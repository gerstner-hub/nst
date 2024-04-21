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

using namespace nst;

#ifdef __AFL_LOOP
__AFL_FUZZ_INIT();
#	define USE_AFL
#endif

Nst::Nst() :
		m_theme{config::THEME},
		m_config_file{m_logger},
		m_pipe_buffer_command{config::EXTERNAL_PIPE_CMDLINE},
		m_wsys{*this},
		m_term{*this},
		m_tty{*this},
		m_selection{*this},
		m_event_handler{*this},
		m_blink_timeout{config::BLINK_TIMEOUT} {
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

namespace {
	// since we are storing std::string_view for color names in the Theme
	// struct we need to store custom colors from the config file
	// somewhere proper. This is a bit ugly but does the trick.
	std::list<std::string> custom_colors;
}

void Nst::loadConfig() {
	m_config_file.parse("/etc/nst.conf");
	if (auto home = cosmos::proc::get_env_var("HOME"); home != std::nullopt) {
		m_config_file.parse(home->str() + "/.config/nst.conf");
	}
	if (m_cmdline.config_file.isSet()) {
		const auto &path = m_cmdline.config_file.getValue();
		if (!m_config_file.parse(path)) {
			m_logger.warn() << "couldn't parse configuration file '" << path
				<< "' supplied on command line\n";
		}
	}
	if (auto conf = cosmos::proc::get_env_var("NST_CONFIG"); conf != std::nullopt) {
		const auto &path = *conf;
		if (!m_config_file.parse(path)) {
			m_logger.warn() << "couldn't parse configuration file '" << path
				<< "' supplied in NST_CONFIG environment variable\n";
		}
	}

	if (auto editor_cmdline = m_config_file.asString("open_buffer_in_editor_cmdline");
			editor_cmdline != std::nullopt) {
		m_pipe_buffer_command = cosmos::split(
				*editor_cmdline, " ", cosmos::SplitFlags{cosmos::SplitFlag::STRIP_PARTS});
	}

	if (auto blink_timeout = m_config_file.asUnsigned("blink_timeout"); blink_timeout != std::nullopt) {
		m_blink_timeout = std::chrono::milliseconds(*blink_timeout);
	}

	if (auto theme_opt = m_config_file.asString("theme"); theme_opt != std::nullopt) {
		bool found = false;

		for (auto &theme: {
				config::DEFAULT_THEME, config::SOLARIZED_LIGHT, config::SOLARIZED_DARK,
				config::NORDTHEME, config::MOONFLY, config::CYBERPUNK_NEON,
				config::DRACULA, config::GRUVBOX}) {
			if (theme.name == *theme_opt) {
				m_theme = theme;
				found = true;
				break;
			}
		}

		if (!found) {
			m_logger.error() << "invalid theme setting '" << *theme_opt << "'\n";
		}
	}

	m_selection.applyConfig();
	m_event_handler.applyConfig();

	// assign basic color overrides from configuration file
	for (size_t colnum = 1; colnum <= m_theme.basic_colors.size(); colnum++) {
		const auto key = cosmos::sprintf("color%zd", colnum);
		if (const auto color_opt = m_config_file.asString(key); color_opt != std::nullopt) {
			auto color = *color_opt;

			custom_colors.push_back(color);
			m_theme.basic_colors[colnum-1] = custom_colors.back();
		}
	}

	// assign extended color overrides from configuration file
	for (size_t colnum = 1; colnum <= 4; colnum++) {
		const auto key = cosmos::sprintf("extcolor%zd", colnum);
		if (const auto color_opt = m_config_file.asString(key); color_opt != std::nullopt) {
			auto color = *color_opt;

			custom_colors.push_back(color);
			if (m_theme.extended_colors.size() < colnum) {
				m_theme.extended_colors.resize(colnum);
			}

			m_theme.extended_colors[colnum-1] = custom_colors.back();
		}
	}

	for (auto &color_pair: {
			std::pair<const char*, ColorIndex&>{"default_fg_color", m_theme.fg},
			                                   {"default_bg_color", m_theme.bg},
			                                   {"default_cursor_color", m_theme.cursor_color},
			                                   {"default_rev_cursor_color", m_theme.reverse_cursor_color}}) {
		const auto key = color_pair.first;
		if (const auto idx_opt = m_config_file.asUnsigned(key); idx_opt != std::nullopt) {
			const auto idx = *idx_opt;
			if (idx == 0 || idx > 256 + 4) {
				m_logger.error() << key << " in config file exceeds maximum color index\n";
			} else {
				color_pair.second = ColorIndex(idx-1);
			}
		}
	}

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
#ifndef USE_AFL
	waitForWindowMapping();
#endif

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

#ifdef USE_AFL
	// this must be assigned outside of the __AFL_LOOP according to the docs
	const unsigned char *testcase = __AFL_FUZZ_TESTCASE_BUF;

	while (__AFL_LOOP(1000)) {

		auto testcase_cur = testcase;
		// this must be assigned like this and not passed directly as
		// function parameter according to the docs
		auto testcase_left = __AFL_FUZZ_TESTCASE_LEN;

	while (testcase_left > 0) {
#else
	while (true) {
#endif
		if (display.hasPendingEvents())
			// existing events might not set the display FD
			timeout = std::chrono::milliseconds(0);

		bool draw_event = false;

#ifndef USE_AFL
		auto events = poller.wait(timeout.count() >= 0 ?
				std::optional<std::chrono::milliseconds>{timeout} :
				std::nullopt);
		bool timedout = events.empty();

		for (const auto &event: events) {
			const auto fd = event.fd();

			if (fd == childfd) {
				m_tty.handleSigChildEvent();
				return;
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
#else
		bool timedout = true;
#endif

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

#ifdef USE_AFL
		auto written = m_term.write(std::string_view{(const char*)testcase_cur, testcase_left}, Term::ShowCtrlChars{false});
		testcase_left -= written;
		testcase_cur += written;
#else
		// idle detected or maxlatency exhausted -> draw
		timeout = std::chrono::milliseconds(-1);

		if (m_blink_timeout.count() > 0 && (m_wsys.isBlinkingCursorStyle() || m_term.existsBlinkingGlyph())) {
			timeout = m_blink_timeout - blink_watch.elapsed();
			if (timeout.count() <= 0) {
				if (-timeout.count() > m_blink_timeout.count()) // start visible
					m_wsys.setBlinking(true);
				m_wsys.switchBlinking();
				m_term.setDirtyByAttr(Attr::BLINK);
				blink_watch.mark();
				timeout = m_blink_timeout;
			}
		}
#endif

		m_term.draw();
		display.flush();
		drawing = false;
#ifdef USE_AFL
		break;
#endif
	}

#ifdef USE_AFL
	m_term.reset();
	m_wsys.resetState();
	}
#endif
}

void Nst::resizeConsole() {
	const auto &twin = m_wsys.termWin();
	auto tdim = twin.getTermDim();

	m_term.resize(tdim);
	m_wsys.resize(tdim);
	m_tty.resize(twin.TTYExtent());
}

void Nst::pipeBufferToExternalCommand() {
	cosmos::Pipe pipe;
	cosmos::ChildCloner cloner;
	cloner.setArgs(m_pipe_buffer_command);
	cloner.setStdIn(pipe.readEnd());

	auto child = cloner.run();
	pipe.closeReadEnd();

	const auto text = m_term.screen().asText(m_term.cursor());
	cosmos::File io{pipe.writeEnd(), cosmos::AutoCloseFD{false}};

	try {
		io.writeAll(text);
	} catch(const cosmos::ApiError &e) {
		if (e.errnum() != cosmos::Errno::BROKEN_PIPE) {
			m_logger.error() << "failed to write terminal buffer to " << m_pipe_buffer_command << ": " << e.what() << "\n";
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

int main(int argc, const char **argv) {
	return cosmos::main<nst::Nst>(argc, argv);
}
