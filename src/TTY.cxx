// C++
#include <cstring>
#include <cstdlib>
#include <ostream>

// cosmos
#include "cosmos/error/ApiError.hxx"
#include "cosmos/error/InternalError.hxx"
#include "cosmos/error/RuntimeError.hxx"
#include "cosmos/error/UsageError.hxx"
#include "cosmos/formatting.hxx"
#include "cosmos/PasswdInfo.hxx"
#include "cosmos/proc/ChildCloner.hxx"
#include "cosmos/proc/process.hxx"
#include "cosmos/proc/SignalFD.hxx"
#include "cosmos/proc/SubProc.hxx"
#include "cosmos/utils.hxx"

// nst
#include "Cmdline.hxx"
#include "nst_config.hxx"
#include "nst.hxx"
#include "TTY.hxx"

namespace nst {

TTY::~TTY() {
	if (m_child_proc.running()) {
		hangup();
		m_cmd_file.close();
		m_child_proc.wait();
	}
}

cosmos::FileDescriptor TTY::create(const Extent extent) {
	const auto &cmdline = m_nst.cmdline();

	if (m_cmd_file.isOpen()) {
		cosmos_throw (cosmos::UsageError("TTY has already been created"));
	}

	setupIOFile(cmdline.iofile.getValue());

	if (auto &line = cmdline.tty_line.getValue(); !line.empty()) {
		// operate on a real TTY line, running stty on it
		openTTY(line);
	} else {
		createPTY(extent);
	}

	m_terminal.setFD(m_cmd_file);
	setupPoller();

	return m_cmd_file.fd();
}

void TTY::openTTY(const std::string &line) {
	try {
		m_cmd_file.open(line, cosmos::OpenMode::READ_WRITE);
	} catch (const std::exception &ex) {
		cosmos_throw (cosmos::ApiError(cosmos::sprintf("open line '%s' failed: %s", line.c_str(), ex.what())));
	}
	m_cmd_file.fd().duplicate(cosmos::stdin, cosmos::CloseOnExec{false});
	configureTTY();
}

void TTY::createPTY(const Extent extent) {
	// create a pseudo TTY
	auto [master, slave] = cosmos::openPTY(toTermDimension(extent));

	m_cmd_file.open(master, cosmos::AutoCloseFD{true});

	try {
		executeShell(slave);
		slave.close();
	} catch(...) {
		m_cmd_file.close();
		slave.close();
		throw;
	}
}

void TTY::setupPoller() {
	if (m_cmd_poller.valid())
		return;

	m_cmd_poller.create();
	m_cmd_poller.addFD(
		m_cmd_file.fd(),
		{
			cosmos::Poller::MonitorFlag::INPUT,
			cosmos::Poller::MonitorFlag::OUTPUT
		}
	);
}

void TTY::setupIOFile(const std::string &path) {
	m_io_file.close();

	if (path == "-") {
		m_io_file.open(cosmos::stdout, cosmos::AutoCloseFD{false});
	} else if (!path.empty()) {
		try {
			m_io_file.open(
				path,
				cosmos::OpenMode::WRITE_ONLY,
				{cosmos::OpenFlag::CREATE, cosmos::OpenFlag::TRUNCATE},
				cosmos::FileMode(cosmos::ModeT{0640})
			);
		} catch (const std::exception &ex) {
			m_nst.logger().error() << "Error opening " << path << ": " << ex.what() << "\n";
		}
	}

	m_nst.term().setPrintMode(m_io_file.isOpen());
}

void TTY::configureTTY() {

	cosmos::ChildCloner cloner;
	{
		auto &args = cloner.getArgs();
		// append fixed config strings
		cosmos::append(args, config::STTY_ARGS);
		// append command line strings
		cosmos::append(args, m_nst.cmdline().rest.getValue());
	}

	try {
		auto res = cloner.run().wait();

		if (!res.exitedSuccessfully()) {
			cosmos_throw (cosmos::RuntimeError("stty returned non-zero"));
		}
	} catch (const std::exception &ex) {
		m_nst.logger().error() << "couldn't call stty: " << ex.what() << "\n";
	}
}

size_t TTY::read() {
	try {
		size_t read_bytes;

		try {
			read_bytes = m_cmd_file.read(m_buf + m_buf_bytes, sizeof(m_buf) - m_buf_bytes);
		} catch (const cosmos::ApiError &ex) {
			if (ex.errnum() == cosmos::Errno::IO_ERROR) {
				// the way the PTY is operated currently
				// causes no EOF condition to be signaled but
				// an EIO is returned. There are different
				// modes the PTY can be operated in, but for
				// the moment let's catch the EIO and
				// translate it into EOF.
				return 0;
			}

			throw;
		}

		if (read_bytes == 0) {
			// EOF, never happens, see above
			return 0;
		}

		// append read bytes to unprocessed bytes
		m_buf_bytes += read_bytes;
		const auto view = std::string_view{m_buf, m_buf_bytes};
		const auto written = m_nst.term().write(view, Term::ShowCtrlChars(false));
		m_buf_bytes -= written;
		// keep any incomplete UTF-8 byte sequence for the next call
		if (m_buf_bytes > 0)
			// NOTE: using a ringbuffer (e.g. via memmap) we could
			// probably avoid this memmove with the trade off of
			// added complexity.
			std::memmove(m_buf, m_buf + written, m_buf_bytes);
		return read_bytes;
	} catch (const std::exception &ex) {
		cosmos_throw (cosmos::RuntimeError(cosmos::sprintf("Couldn't read from shell: %s", ex.what())));
		return 0;
	}
}

void TTY::write(const std::string_view sv, const MayEcho echo) {

	auto &term = m_nst.term();
	const auto mode = term.mode();

	if (echo && mode[Term::Mode::TECHO])
		// display data on screen
		term.write(sv, Term::ShowCtrlChars{true});

	if (!mode[Term::Mode::CRLF]) {
		// forward unmodified data to child
		writeRaw(sv);
		return;
	}

	// otherwise we need to translate newlines

	// This is similar to how the kernel handles ONLCR for ttys
	for (auto it = sv.begin(); it < sv.end();) {
		if (*it == '\r') {
			it++;
			writeRaw("\r\n");
		} else {
			// write the segment up to the next CR
			auto next = std::find(it, sv.end(), '\r');
			writeRaw({&(*it), static_cast<size_t>(next - it)});
			it = next;
		}
	}
}

void TTY::writeRaw(const std::string_view sv) {
	// Remember that we are potentially using a real TTY, which might be a modem line.
	// Writing too much will clog the line. That's why we are doing this dance.
	// FIXME: Migrate the world to Plan 9.
	using Event = cosmos::Poller::Event;
	const char *data = sv.data();
	size_t written;

	for (size_t limit = 256, left = sv.size(); left > 0; ) {
		for (const auto &event: m_cmd_poller.wait()) {

			const auto events = event.getEvents();

			if (events & Event::OUTPUT_READY) {
				// Only write the bytes written by write() or the
				// default of 256. This seems to be a reasonable value
				// for a serial line. Bigger values might clog the I/O.
				//
				// TODO: since Modem lines are not likely to
				// be used any more this is causing a lot of
				// system call overhead in case of a PTY.
				// Using a simpler algorithm for PTYs might be
				// more efficient.
				written = m_cmd_file.write(data, std::min(left, limit));
				if (written == left)
					// All bytes have been written.
					return;

				// We weren't able to write out everything.
				// This means the buffer is getting full
				// again. Empty it.
				if (left < limit)
					limit = this->read();
				left -= written;
				data += written;
			}

			// NOTE: the order of output/input is important, we
			// need to prefer writes, otherwise we clog our own
			// input buffer until it's full, and nothing is ever
			// written out.
			if (events & Event::INPUT_READY) {
				limit = this->read();
			}
		}
	}
}

cosmos::TermDimension TTY::toTermDimension(const Extent size) const {
	const auto &term = m_nst.term();
	cosmos::TermDimension dim(term.numCols(), term.numRows());
	// according to the man page these fields are unused on Linux, but it
	// seems nst wants to use them anyway
	dim.ws_xpixel = size.width;
	dim.ws_ypixel = size.height;

	return dim;
}

void TTY::resize(const Extent size) {
	try {
		m_terminal.setSize(toTermDimension(size));
	} catch (const std::exception &ex) {
		m_nst.logger().error() << "Couldn't set TTY window size: " << ex.what() << "\n";
	}
}

void TTY::hangup() {
	// Send SIGHUP to shell
	m_child_proc.kill(cosmos::signal::HANGUP);
}

void TTY::executeShell(cosmos::FileDescriptor slave) {

	cosmos::PasswdInfo pw_info{cosmos::proc::get_real_user_id()};
	if (!pw_info.valid()) {
		cosmos_throw (cosmos::InternalError("who are you?"));
	}

	cosmos::SysString shell;
	if (auto sh = std::getenv("SHELL"); sh != nullptr)
		shell = sh;
	else
		// try the shell from passwd
		shell = pw_info.shell();

	// if still empty then use compile time default
	if (shell.empty()) {
		shell = config::SHELL.data();
	}

	cosmos::ChildCloner cloner;

	// code executed in the child before we execute the new program
	cloner.setPostForkCB([this, &slave, pw_info, shell](const cosmos::ChildCloner &) {
		// close unnecessary file descriptors in the child
		m_io_file.close();
		m_cmd_file.close();

		// create a new process group
		cosmos::proc::create_new_session();

		// make the slave end of the TTY the new default file descriptors for the child
		for (auto stdfd: {cosmos::stdin, cosmos::stdout, cosmos::stderr}) {
			slave.duplicate(stdfd, cosmos::CloseOnExec(false));
		}

		// make our new TTY the controlling terminal of the child
		cosmos::Terminal(slave).makeControllingTerminal();

		// make sure no unnecessary duplicate of the slave TTY exists
		if (slave.raw() > cosmos::FileNum{2})
			slave.close();

		using namespace cosmos;

		// restore default signal handlers
		for (auto sig: {
				signal::CHILD, signal::HANGUP, signal::INTERRUPT,
				signal::QUIT, signal::TERMINATE, signal::ALARM}) {
			cosmos::signal::restore(cosmos::Signal{sig});
		}

		for (const auto var: {"COLUMNS", "LINES", "TERMCAP"}) {
			unsetenv(var);
		}

		const proc::OverwriteEnv overwrite{true};

		proc::set_env_var("LOGNAME", pw_info.name(),           overwrite);
		proc::set_env_var("USER",    pw_info.name(),           overwrite);
		proc::set_env_var("SHELL",   shell,                    overwrite);
		proc::set_env_var("HOME",    pw_info.homeDir(),        overwrite);
		proc::set_env_var("TERM",    config::TERM_NAME.data(), overwrite);
	});

	if (auto &args = m_nst.cmdline().rest.getValue(); !args.empty()) {
		cloner.setArgs(args);
	} else {
		// use default configuration
		if (!config::SCROLL.empty()) {
			cloner.setArgsFromView({config::SCROLL, config::UTMP.empty() ? shell : config::UTMP});
		} else if (!config::UTMP.empty()) {
			cloner.setExe(config::UTMP);
		} else {
			cloner.setExe(shell);
		}
	}

	// this may throw, we'll let it pass through to the caller
	m_child_proc = cloner.run();
}

void TTY::handleSigChildEvent() {
	auto res = m_child_proc.wait();

	if (!res.exitedSuccessfully()) {
		cosmos_throw (cosmos::RuntimeError(
			cosmos::sprintf("child exited with status %d",
				cosmos::to_integral(res.exitStatus()))));
	} else if (res.signaled()) {
		cosmos_throw (cosmos::RuntimeError(
			cosmos::sprintf("child terminated due to signal %d",
				cosmos::to_integral(res.termSignal().raw()))));
	}
}

void TTY::sendBreak() {
	try {
		m_terminal.sendBreak(std::chrono::milliseconds{0});
	} catch (const std::exception &ex) {
		m_nst.logger().error() << "failed to send break: " << ex.what() << "\n";
	}
}

void TTY::doPrintToIoFile(const std::string_view s) {
	try {
		m_io_file.writeAll(s);
	} catch (const std::exception &ex) {
		m_nst.logger().error() << "error writing to I/O file: " << ex.what() << ". Closing I/O file\n";
		m_io_file.close();
	}
}

} // end ns
