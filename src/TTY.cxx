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
#include <iostream>
#include <sstream>

// nst
#include "TTY.hxx"
#include "nst_config.h"
#include "st.h"

// libcosmos
#include "cosmos/errors/ApiError.hxx"
#include "cosmos/errors/InternalError.hxx"
#include "cosmos/errors/RuntimeError.hxx"
#include "cosmos/proc/SubProc.hxx"
#include "cosmos/formatting.hxx"
#include "cosmos/proc/Process.hxx"
#include "cosmos/algs.hxx"

nst::TTY g_tty;

namespace nst {

using cosmos::ApiError;

// TODO: check if we can C++ify this callback
void sendbreak(const Arg *) {
	g_tty.sendBreak();
}

TTY::~TTY() {
	if (m_child_proc.running()) {
		hangup();
		close(m_cmdfd);
		m_child_proc.wait();
	}
}

int TTY::create(const Params &pars) {

	setupIOFile(pars.out);

	// operate an a real TTY line, running stty on it
	if (!pars.line.empty()) {
		if ((m_cmdfd = open(pars.line.c_str(), O_RDWR)) < 0) {
			cosmos_throw (ApiError(cosmos::sprintf("open line '%s' failed", pars.line.c_str())));
		}
		dup2(m_cmdfd, STDIN_FILENO);
		runStty(pars);
		return m_cmdfd;
	}

	// create a pseudo TTY
	int master, slave;

	/* seems to work fine on linux, openbsd and freebsd */
	if (openpty(&master, &slave, NULL, NULL, NULL) < 0) {
		cosmos_throw (ApiError("openpty failed"));
	}

	m_cmdfd = master;

	cosmos::g_process.blockSignals({cosmos::Signal(SIGCHLD)});

	try {
		executeShell(pars, slave);
	} catch(...) {
		close(m_cmdfd);
		close(slave);
		throw;
	}

	close(slave);

	return m_cmdfd;
}

void TTY::setupIOFile(const std::string &path) {
	if (path.empty()) {
		m_io_file.close();
		return;
	}

	m_term->mode.set(Term::Mode::PRINT);

	if (path == "-")
	{
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
	}
	catch (const std::exception &ex) {
		std::cerr << "Error opening " << path << ": " << ex.what() << std::endl;
	}
}

void TTY::runStty(const Params &pars) {
	cosmos::SubProc stty;
	auto &args = stty.args();
	// append fixed config strings
	cosmos::append(args, config::STTY_ARGS);
	// append STL strings
	cosmos::append(args, pars.args);

	try {
		stty.run();
		auto res = stty.wait();

		if (res.exited() || res.exitStatus() != 0) {
			cosmos_throw (cosmos::RuntimeError("stty returned non-zero"));
		}
	}
	catch (const std::exception &ex) {
		std::cerr << "couldn't call stty: " << ex.what() << std::endl;
	}
}

size_t TTY::read() {
	static char buf[BUFSIZ];
	static int buflen = 0;
	int ret, written;

	/* append read bytes to unprocessed bytes */
	ret = ::read(m_cmdfd, buf+buflen, sizeof(buf)-buflen);

	switch (ret) {
	case 0:
		// EOF
		exit(0);
	case -1:
		cosmos_throw (ApiError("couldn't read from shell"));
		return -1;
	default:
		buflen += ret;
		written = term.write(buf, buflen, 0);
		buflen -= written;
		/* keep any incomplete UTF-8 byte sequence for the next call */
		if (buflen > 0)
			memmove(buf, buf + written, buflen);
		return ret;
	}
}

void TTY::write(const char *s, size_t n, bool may_echo) {

	if (may_echo && m_term->mode.test(Term::Mode::TECHO))
		term.write(s, n, 1);

	if (!m_term->mode.test(Term::Mode::CRLF)) {
		writeRaw(s, n);
		return;
	}

	const char *next;

	/* This is similar to how the kernel handles ONLCR for ttys */
	while (n > 0) {
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
	fd_set wfd, rfd;
	ssize_t r;
	size_t lim = 256;

	/*
	 * Remember that we are using a pty, which might be a modem line.
	 * Writing too much will clog the line. That's why we are doing this
	 * dance.
	 * FIXME: Migrate the world to Plan 9.
	 */
	while (n > 0) {
		FD_ZERO(&wfd);
		FD_ZERO(&rfd);
		FD_SET(m_cmdfd, &wfd);
		FD_SET(m_cmdfd, &rfd);

		/* Check if we can write. */
		if (pselect(m_cmdfd+1, &rfd, &wfd, nullptr, nullptr, nullptr) < 0) {
			if (errno == EINTR)
				continue;
			cosmos_throw (ApiError("select failed"));
		}
		if (FD_ISSET(m_cmdfd, &wfd)) {
			/*
			 * Only write the bytes written by write() or the
			 * default of 256. This seems to be a reasonable value
			 * for a serial line. Bigger values might clog the I/O.
			 */
			if ((r = ::write(m_cmdfd, s, std::min(n, lim))) < 0)
				cosmos_throw (ApiError("write error on tty"));
			if ((size_t)r < n) {
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
				break;
			}
		}
		if (FD_ISSET(m_cmdfd, &rfd))
			lim = this->read();
	}
}

void TTY::resize(int tw, int th) {
	struct winsize w;

	w.ws_row = m_term->row;
	w.ws_col = m_term->col;
	w.ws_xpixel = tw;
	w.ws_ypixel = th;

	if (ioctl(m_cmdfd, TIOCSWINSZ, &w) < 0) {
		std::cerr << "Couldn't set window size: " << strerror(errno) << "\n";
	}
}

void TTY::hangup() {
	/* Send SIGHUP to shell */
	m_child_proc.kill(cosmos::Signal(SIGHUP));
}

void TTY::executeShell(const Params &pars, int slave)
{
	const struct passwd *pw = nullptr;

	errno = 0;
	if ((pw = getpwuid(getuid())) == NULL) {
		if (errno)
			cosmos_throw (ApiError(("getpwuid failed")));
		else
			cosmos_throw (cosmos::InternalError("who are you?"));
	}

	const char *sh = getenv("SHELL");
	if(!sh) {
		if (pw->pw_shell[0])
			sh = pw->pw_shell;
		else
			sh = pars.cmd.c_str();
	}

	m_child_proc.setPostForkCB([this, slave, pars, pw, sh](const cosmos::SubProc &proc) {
		m_io_file.close();
		close(m_cmdfd);
		setsid(); /* create a new process group */
		for (auto stdfd: {STDIN_FILENO, STDOUT_FILENO, STDERR_FILENO}) {
			dup2(slave, stdfd);
		}
		if (ioctl(slave, TIOCSCTTY, nullptr) < 0) {
			cosmos_throw (ApiError("ioctl TIOCSCTTY failed"));
		}
		if (slave > 2)
			close(slave);

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

	if (pars.args.empty()) {
		// use default configuration
		if (config::SCROLL) {
			m_child_proc.setArgs({config::SCROLL, config::UTMP ? config::UTMP : sh});
		} else if (config::UTMP) {
			m_child_proc.setExe(std::string(config::UTMP));
		} else {
			m_child_proc.setExe(sh);
		}
	} else {
		m_child_proc.setArgs(pars.args);
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
	if (tcsendbreak(m_cmdfd, 0))
		perror("Error sending break");
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
