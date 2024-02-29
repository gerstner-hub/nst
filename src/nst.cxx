// cosmos
#include "cosmos/error/ApiError.hxx"
#include "cosmos/formatting.hxx"
#include "cosmos/fs/filesystem.hxx"
#include "cosmos/io/Pipe.hxx"
#include "cosmos/io/Poller.hxx"
#include "cosmos/io/StreamIO.hxx"
#include "cosmos/locale.hxx"
#include "cosmos/proc/ChildCloner.hxx"
#include "cosmos/proc/process.hxx"
#include "cosmos/proc/Signal.hxx"
#include "cosmos/proc/SigSet.hxx"
#include "cosmos/time/time.hxx"
#include "cosmos/utils.hxx"

// xpp
#include "xpp/XDisplay.hxx"
#include "xpp/event/ConfigureEvent.hxx"

// nst
#include "nst.hxx"
#include "IpcHandler.hxx"

namespace nst {

Nst::Nst() :
		m_config_file{m_logger},
		m_wsys{*this},
		m_term{*this},
		m_tty{*this},
		m_selection{*this},
		m_event_handler{*this} {
	auto pid = cosmos::proc::get_own_pid();
	m_logger.setPrefix(cosmos::sprintf("nst[%d] ", cosmos::to_integral(pid)));
}

void Nst::waitForWindowMapping() {
	xpp::Event ev;

	do {
		xpp::display.nextEvent(ev);
		// This XFilterEvent call is required because of XOpenIM. It
		// does filter out the key event and some client message for
		// the input method too.
		if (ev.filterEvent())
			continue;
		else if (ev.isConfigureNotify()) {
			xpp::ConfigureEvent configure{ev};
			m_wsys.setWinSize(Extent{configure.extent()});
		}
	} while (!ev.isMapNotify());
}

cosmos::ExitStatus Nst::main(int argc, const char **argv) {
	cosmos::locale::set_from_environment(cosmos::locale::Category::CTYPE);
	// initializes the X locale handling, mostly supports setting the
	// input method e.g. via XMODIFIERS environment variable
	::XSetLocaleModifiers("");

	m_cmdline.parse(argc, argv);
	if (m_cmdline.cwd.isSet()) {
		try {
			cosmos::fs::change_dir(m_cmdline.cwd.getValue());
		} catch (const cosmos::CosmosError &ex) {
			m_logger.warn() << "could not enter CWD " << m_cmdline.cwd.getValue() << ": "
				<< ex.what() << "\n";
		}
	}
	setupSignals();
	loadConfig();
	m_wsys.init();
	m_term.init(*this);
	setEnv();
	mainLoop();
	return cosmos::ExitStatus::SUCCESS;
}

void Nst::loadConfig() {
	m_config_file.parse("/etc/nst.conf");
	if (auto home = cosmos::proc::get_env_var("HOME"); home != std::nullopt) {
		m_config_file.parse(home->str() + "/.config/nst.conf");
	}

	m_selection.applyConfig();
	m_event_handler.applyConfig();
}

void Nst::setEnv() {
	auto win = m_wsys.window().id();
	cosmos::proc::set_env_var(
			"WINDOWID",
			std::to_string(cosmos::to_integral(win)).c_str(),
			cosmos::proc::OverwriteEnv{true});

	if constexpr (config::ENABLE_IPC) {
		cosmos::proc::set_env_var(
				"NST_IPC_ADDR",
				IpcHandler::address(),
				/* if we run nested nst sessions we need to overwrite this */
				cosmos::proc::OverwriteEnv{true});
	}
}

void Nst::setupSignals() {
	// we want to receive SIGCHLD synchronously via a pid FD, so block it
	cosmos::signal::block(cosmos::SigSet{cosmos::signal::CHILD});
	// we might use pipes, don't send async signals if they break
	cosmos::signal::block(cosmos::SigSet{cosmos::signal::PIPE});
}

void Nst::mainLoop() {
	cosmos::Poller poller;

	bool drawing = false;
	cosmos::MonotonicStopWatch draw_watch;
	cosmos::MonotonicStopWatch blink_watch{cosmos::MonotonicStopWatch::InitialMark{true}};
	std::chrono::milliseconds timeout{-1};

	poller.create();
	waitForWindowMapping();

	// don't create the TTY before we know the proper initial TTY size
	// from X11, otherwise child processes that evaluate the TTY size
	// might race against waitForWindowMapping() causing irritating
	// behaviour (e.g. `less` behaves strange if the TTY has a 0/0 size).
	auto ttyfd = m_tty.create(m_wsys.termWin().TTYExtent());
	auto childfd = m_tty.childFD();
	auto &display = xpp::display;

	resizeConsole();

	for (auto fd: {
			ttyfd,
			display.connectionNumber(),
			static_cast<cosmos::FileDescriptor&>(childfd)}) {
		poller.addFD(fd, {cosmos::Poller::MonitorFlag::INPUT});
	}

	std::unique_ptr<IpcHandler> ipc_handler;

	if constexpr (config::ENABLE_IPC) {
		ipc_handler.reset(new IpcHandler{*this, poller});
		ipc_handler->init();
	}

	while (true) {
		if (display.hasPendingEvents())
			// existing events might not set the display FD
			timeout = std::chrono::milliseconds(0);

		auto events = poller.wait(timeout.count() >= 0 ?
				std::optional<std::chrono::milliseconds>{timeout} :
				std::nullopt);

		bool draw_event = false;
		bool timedout = events.empty();

		for (const auto &event: events) {
			const auto fd = event.fd();

			if (fd == childfd) {
				m_tty.handleSigChildEvent();
			} else if (fd == ttyfd) {
				if (m_tty.read() == 0)
					// EOF condition
					return;
				draw_event = true;
			} else if (fd == display.connectionNumber()) {
				// handled below
			} else if (ipc_handler) {
				ipc_handler->checkEvent(event);
			}
		}

		draw_event |= m_event_handler.checkEvents();

		// To reduce flicker and tearing, when new content or an event
		// triggers drawing, we first wait a bit to ensure we got
		// everything, and if nothing new arrives - we draw.
		// We start with trying to wait MIN_LATENCY ms. If more content
		// arrives sooner, we retry with shorter and shorter periods,
		// and eventually draw even without idle after MAX_LATENCY ms.
		// Typically this results in low latency while interacting,
		// maximum latency intervals during `cat huge.txt`, and perfect
		// sync with periodic updates from animations/key-repeats/etc.
		if (draw_event) {
			if (!drawing) {
				draw_watch.mark();
				drawing = true;
				m_wsys.setBlinking(false);
			}

			const auto diff = draw_watch.elapsed();
			timeout = (config::MAX_LATENCY - diff) / config::MAX_LATENCY * config::MIN_LATENCY;

			if (timeout.count() > 0)
				// we have time, try to find idle
				continue;
		} else if (!timedout) {
			continue;
		}

		// idle detected or maxlatency exhausted -> draw
		timeout = std::chrono::milliseconds(-1);

		if (config::BLINK_TIMEOUT.count() > 0 && (m_wsys.isBlinkingCursorStyle() || m_term.existsBlinkingGlyph())) {
			timeout = config::BLINK_TIMEOUT - blink_watch.elapsed();
			if (timeout.count() <= 0) {
				if (-timeout.count() > config::BLINK_TIMEOUT.count()) // start visible
					m_wsys.setBlinking(true);
				m_wsys.switchBlinking();
				m_term.setDirtyByAttr(Attr::BLINK);
				blink_watch.mark();
				timeout = config::BLINK_TIMEOUT;
			}
		}

		m_term.draw();
		display.flush();
		drawing = false;
	}
}

void Nst::resizeConsole() {
	const auto &twin = m_wsys.termWin();
	auto tdim = twin.getTermDim();

	m_term.resize(tdim);
	m_wsys.resize(tdim);
	m_tty.resize(twin.TTYExtent());
}

void Nst::pipeBufferTo(const cosmos::StringViewVector cmdline) {
	cosmos::Pipe pipe;
	cosmos::ChildCloner cloner;
	cloner.setArgsFromView(cmdline);
	cloner.setStdIn(pipe.readEnd());

	auto child = cloner.run();
	pipe.closeReadEnd();

	const auto text = m_term.screen().asText(m_term.cursor());
	cosmos::File io{pipe.writeEnd(), cosmos::AutoCloseFD{false}};

	try {
		io.writeAll(text);
	} catch(const cosmos::ApiError &e) {
		if (e.errnum() != cosmos::Errno::BROKEN_PIPE) {
			m_logger.error() << "failed to write terminal buffer to " << cmdline << ": " << e.what() << "\n";
		}
	}

	if (auto res = child.wait(); !res.exitedSuccessfully()) {
		auto &errlog = m_logger.error();
		errlog << "pipe sub process exited unsuccessfully: ";
		if (res.exited()) {
			errlog << "code = " << res.exitStatus() << "\n";
		} else {
			errlog << "signal = " << res.termSignal() << "\n";
		}
	}
}

} // end ns nst

int main(int argc, const char **argv) {
	return cosmos::main<nst::Nst>(argc, argv);
}
