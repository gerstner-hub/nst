// Linux
#include <fcntl.h>
#include <limits.h>
#include <pty.h>
#include <pwd.h>
#include <signal.h>
#include <unistd.h>

// libc
#include <string.h>

// stdlib
#include <cstring>
#include <iostream>
#include <sstream>

// nst
#include "Cmdline.hxx"
#include "TTY.hxx"
#include "nst.hxx"
#include "nst_config.h"

// libcosmos
#include "cosmos/algs.hxx"
#include "cosmos/errors/ApiError.hxx"
#include "cosmos/errors/InternalError.hxx"
#include "cosmos/errors/RuntimeError.hxx"
#include "cosmos/formatting.hxx"
#include "cosmos/proc/Process.hxx"
#include "cosmos/proc/SubProc.hxx"

namespace nst {

using cosmos::ApiError;

// TODO: check if we can C++ify this callback
void sendbreak(const Arg *) {
	Nst::getTTY().sendBreak();
}

namespace {

	std::pair<cosmos::FileDescriptor, cosmos::FileDescriptor> openPTY() {
		int master, slave;
		/* seems to work fine on linux, openbsd and freebsd */
		if (openpty(&master, &slave, nullptr, nullptr, nullptr) < 0) {
			cosmos_throw (ApiError("openpty failed"));
		}

		return {cosmos::FileDescriptor(master), cosmos::FileDescriptor(slave)};
	}
}

TTY::~TTY() {
	if (m_child_proc.running()) {
		hangup();
		m_cmd_file.close();
		m_child_proc.wait();
	}
}

cosmos::FileDescriptor TTY::create(const Cmdline &cmdline) {

	setupIOFile(cmdline.iofile.getValue());

	// operate an a real TTY line, running stty on it
	if (!cmdline.tty_line.getValue().empty()) {
		auto &line = cmdline.tty_line.getValue();
		try {
			m_cmd_file.open(line, cosmos::OpenMode::READ_WRITE);
		} catch (const std::exception &ex) {
			cosmos_throw (ApiError(cosmos::sprintf("open line '%s' failed: %s", line.c_str(), ex.what())));
		}
		m_pty.setFD(m_cmd_file);
		m_cmd_file.getFD().duplicate(cosmos::stdin, /*cloexec=*/false);
		runStty(cmdline);
		return m_cmd_file.getFD();
	}

	// create a pseudo TTY
	auto [master, slave] = openPTY();

	m_cmd_file.open(master, /*closefd=*/true);
	m_pty.setFD(m_cmd_file);

	try {
		executeShell(cmdline, slave);
		slave.close();
	} catch(...) {
		m_cmd_file.close();
		slave.close();
		throw;
	}

	return m_cmd_file.getFD();
}

void TTY::setupIOFile(const std::string &path) {
	if (path.empty()) {
		m_io_file.close();
		return;
	}

	m_term->setPrintMode(true);

	if (path == "-") {
		m_io_file = cosmos::StreamFile(cosmos::stdout, false);
		return;
	}

	try {
		m_io_file.open(
			path,
			cosmos::OpenMode::WRITE_ONLY,
			cosmos::OpenFlags({cosmos::OpenSettings::CREATE, cosmos::OpenSettings::TRUNCATE}),
			cosmos::FileMode(0640)
		);
	} catch (const std::exception &ex) {
		std::cerr << "Error opening " << path << ": " << ex.what() << std::endl;
	}
}

void TTY::runStty(const Cmdline &cmdline) {
	cosmos::SubProc stty;
	auto &args = stty.args();
	// append fixed config strings
	cosmos::append(args, config::STTY_ARGS);
	// append command line strings
	cosmos::append(args, cmdline.rest.getValue());

	try {
		stty.run();
		auto res = stty.wait();

		if (res.exited() || res.exitStatus() != 0) {
			cosmos_throw (cosmos::RuntimeError("stty returned non-zero"));
		}
	} catch (const std::exception &ex) {
		std::cerr << "couldn't call stty: " << ex.what() << std::endl;
	}
}

size_t TTY::read() {
	/* append read bytes to unprocessed bytes */
	try {
		switch (auto ret = m_cmd_file.read(m_buf + m_buf_bytes, sizeof(m_buf) - m_buf_bytes)) {
		case 0:
			// EOF
			exit(0);
		default:
			m_buf_bytes += ret;
			auto written = m_term->write(m_buf, m_buf_bytes, 0);
			m_buf_bytes -= written;
			/* keep any incomplete UTF-8 byte sequence for the next call */
			if (m_buf_bytes > 0)
				std::memmove(m_buf, m_buf + written, m_buf_bytes);
			return ret;
		}
	} catch (const std::exception &ex) {
		cosmos_throw (cosmos::RuntimeError(cosmos::sprintf("Couldn't read from shell: %s", ex.what())));
		return 0;
	}
}

void TTY::write(const char *s, size_t n, bool may_echo) {

	auto &mode = m_term->getMode();

	if (may_echo && mode[Term::Mode::TECHO])
		m_term->write(s, n, 1);

	if (!mode[Term::Mode::CRLF]) {
		writeRaw(s, n);
		return;
	}

	/* This is similar to how the kernel handles ONLCR for ttys */
	for (const char *next; n > 0;) {
		if (*s == '\r') {
			next = s + 1;
			writeRaw("\r\n", 2);
		} else {
			next = (const char*)memchr(s, '\r', n);
			if (!next)
				next = s + n;
			writeRaw(s, next - s);
		}
		n -= next - s;
		s = next;
	}
}

void TTY::writeRaw(const char *s, size_t n) {
	if (!m_cmd_poller.isValid()) {
		m_cmd_poller.create();
		m_cmd_poller.addFD(
			m_cmd_file.getFD(),
			cosmos::Poller::MonitorMask({
				cosmos::Poller::MonitorSetting::INPUT,
				cosmos::Poller::MonitorSetting::OUTPUT
			})
		);
	}

	/*
	 * Remember that we are using a pty, which might be a modem line.
	 * Writing too much will clog the line. That's why we are doing this
	 * dance.
	 * FIXME: Migrate the world to Plan 9.
	 */
	using Event = cosmos::Poller::Event;
	size_t r;

	for (size_t lim = 256; n > 0; ) {
		for (const auto &event: m_cmd_poller.wait()) {

			const auto events = event.getEvents();

			if (events.test(Event::OUTPUT_READY)) {
				/*
				 * Only write the bytes written by write() or the
				 * default of 256. This seems to be a reasonable value
				 * for a serial line. Bigger values might clog the I/O.
				 */
				r = m_cmd_file.write(s, std::min(n, lim));
				if (r < n) {
					/*
					 * We weren't able to write out everything.
					 * This means the buffer is getting full
					 * again. Empty it.
					 */
					if (n < lim)
						lim = this->read();
					n -= r;
					s += r;
				} else {
					/* All bytes have been written. */
					return;
				}
			}

			// NOTE: the order of output/input is important, we
			// need to prefer writes, otherwise we clog our own
			// input buffer until it's full, and nothing is every
			// written out.
			if (events.test(Event::INPUT_READY)) {
				lim = this->read();
			}

		}
	}
}

void TTY::resize(size_t tw, size_t th) {
	cosmos::TermDimension dim(m_term->getNumCols(), m_term->getNumRows());
	// according to the man page these fields are unused, but it seems nst
	// wants to use them anyway
	dim.ws_xpixel = tw;
	dim.ws_ypixel = th;

	try {
		m_pty.setSize(dim);
	} catch (const std::exception &ex) {
		std::cerr << "Couldn't set window size: " << ex.what() << "\n";
	}
}

void TTY::hangup() {
	/* Send SIGHUP to shell */
	m_child_proc.kill(cosmos::Signal(SIGHUP));
}

void TTY::executeShell(const Cmdline &cmdline, cosmos::FileDescriptor slave)
{
	const struct passwd *pw = nullptr;

	cosmos::g_process.blockSignals({cosmos::Signal(SIGCHLD)});

	errno = 0;
	if ((pw = getpwuid(getuid())) == NULL) {
		if (errno)
			cosmos_throw (ApiError(("getpwuid failed")));
		else
			cosmos_throw (cosmos::InternalError("who are you?"));
	}

	const char *sh = getenv("SHELL");
	if (!sh) {
		if (pw->pw_shell[0])
			sh = pw->pw_shell;
		else
			sh = nst::config::SHELL;
	}

	m_child_proc.setPostForkCB([this, &slave, pw, sh](const cosmos::SubProc &) {
		m_io_file.close();
		m_cmd_file.close();

		setsid(); /* create a new process group */

		for (auto stdfd: {cosmos::stdin, cosmos::stdout, cosmos::stderr}) {
			slave.duplicate(stdfd, /*cloexec=*/false);
		}

		if (ioctl(slave.raw(), TIOCSCTTY, nullptr) < 0) {
			cosmos_throw (ApiError("ioctl TIOCSCTTY failed"));
		}

		if (slave.raw() > 2) {
			slave.close();
		}

		for (auto sig: {SIGCHLD, SIGHUP, SIGINT, SIGQUIT, SIGTERM, SIGALRM}) {
			::signal(sig, SIG_DFL);
		}

		unsetenv("COLUMNS");
		unsetenv("LINES");
		unsetenv("TERMCAP");
		setenv("LOGNAME", pw->pw_name, 1);
		setenv("USER", pw->pw_name, 1);
		setenv("SHELL", sh, 1);
		setenv("HOME", pw->pw_dir, 1);
		setenv("TERM", config::TERMNAME, 1);
	});

	auto &args = cmdline.rest.getValue();

	if (args.empty()) {
		// use default configuration
		if (config::SCROLL) {
			m_child_proc.setArgs({config::SCROLL, config::UTMP ? config::UTMP : sh});
		} else if (config::UTMP) {
			m_child_proc.setExe(std::string(config::UTMP));
		} else {
			m_child_proc.setExe(sh);
		}
	} else {
		m_child_proc.setArgs(args);
	}

	// this may throw, we'll let it pass through to the caller
	m_child_proc.run();
}

void TTY::sigChildEvent() {
	auto res = m_child_proc.wait();

	if (res.exited() && res.exitStatus() != 0) {
		cosmos_throw (cosmos::RuntimeError(cosmos::sprintf("child exited with status %d", res.exitStatus())));
	} else if (res.signaled()) {
		cosmos_throw (cosmos::RuntimeError(cosmos::sprintf("child terminated due to signal %d", res.termSignal().raw())));
	}

	_exit(0);
}

void TTY::sendBreak() {
	try {
		m_pty.sendBreak(0);
	} catch (const std::exception &ex) {
		std::cerr << "failed to send break: " << ex.what() << std::endl;
	}
}

void TTY::doPrintToIoFile(const char *s, size_t len) {
	try {
		m_io_file.writeAll(s, len);
	} catch (const std::exception &ex) {
		std::cerr << "error writing to output file: " << ex.what() << std::endl;
		m_io_file.close();
	}
}

} // end ns
