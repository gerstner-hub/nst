// cosmos
#include "cosmos/algs.hxx"
#include "cosmos/io/Poller.hxx"
#include "cosmos/proc/process.hxx"
#include "cosmos/time/time.hxx"

// nst
#include "nst.hxx"

namespace nst {

Nst::Nst() :
		m_x11{*this},
		m_term{*this},
		m_tty{*this},
		m_selection{*this},
		m_event_handler{*this}
{}

void Nst::waitForWindowMapping() {
	xpp::Event ev;
	auto &display = m_x11.display();

	/* Waiting for window mapping */
	do {
		display.nextEvent(ev);
		/*
		 * This XFilterEvent call is required because of XOpenIM. It
		 * does filter out the key event and some client message for
		 * the input method too.
		 */
		if (ev.filterEvent())
			continue;
		else if (ev.isConfigureNotify()) {
			const auto &configure = ev.toConfigureNotify();
			m_x11.setWinSize(Extent{configure.width, configure.height});
		}
	} while (!ev.isMapNotify());

	resizeConsole(m_x11.termWin().winExtent());
}

void Nst::run(int argc, const char **argv) {
	setlocale(LC_CTYPE, "");
	XSetLocaleModifiers("");

	m_cmdline.parse(argc, argv);
	m_term.init(*this);
	m_x11.init();
	setEnv();
	mainLoop();
}

void Nst::setEnv() {
	auto win = m_x11.window().id();
	cosmos::proc::set_env_var(
			"WINDOWID",
			std::to_string(cosmos::to_integral(win)).c_str(),
			cosmos::proc::OverwriteEnv{true});
}


void Nst::mainLoop() {
	auto ttyfd = m_tty.create();
	auto childfd = m_tty.childFD();
	auto &display = m_x11.display();
	auto xfd = display.connectionNumber();

	cosmos::Poller poller;
	poller.create();
	for (auto fd: {ttyfd, xfd, childfd}) {
		poller.addFD(fd, cosmos::Poller::MonitorMask{{cosmos::Poller::MonitorSetting::INPUT}});
	}

	xpp::Event ev;
	bool drawing = false;
	cosmos::MonotonicStopWatch draw_watch;
	cosmos::MonotonicStopWatch blink_watch{cosmos::MonotonicStopWatch::InitialMark{true}};
	std::chrono::milliseconds timeout{-1};

	waitForWindowMapping();

	while (true) {
		if (display.hasPendingEvents())
			timeout = std::chrono::milliseconds(0);  /* existing events might not set xfd */

		auto events = poller.wait(timeout.count() >= 0 ?
				std::optional<std::chrono::milliseconds>{timeout} :
				std::nullopt);

		bool draw_event = false;

		for (const auto &event: events) {
			if (event.fd() == childfd) {
				m_tty.handleSigChildEvent();
			} else if (event.fd() == ttyfd) {
				if (m_tty.read() == 0)
					// EOF condition
					return;
				draw_event = true;
			}
		}

		while (display.hasPendingEvents()) {
			draw_event = true;
			display.nextEvent(ev);
			if (ev.filterEvent())
				continue;
			m_event_handler.process(ev);
		}

		/*
		 * To reduce flicker and tearing, when new content or event
		 * triggers drawing, we first wait a bit to ensure we got
		 * everything, and if nothing new arrives - we draw.
		 * We start with trying to wait minlatency ms. If more content
		 * arrives sooner, we retry with shorter and shorter periods,
		 * and eventually draw even without idle after MAX_LATENCY ms.
		 * Typically this results in low latency while interacting,
		 * maximum latency intervals during `cat huge.txt`, and perfect
		 * sync with periodic updates from animations/key-repeats/etc.
		 */
		if (draw_event) {
			if (!drawing) {
				draw_watch.mark();
				drawing = true;
			}

			const auto diff = draw_watch.elapsed();
			timeout = (config::MAX_LATENCY - diff) / config::MAX_LATENCY * config::MIN_LATENCY;

			if (timeout.count() > 0)
				continue;  /* we have time, try to find idle */
		}

		/* idle detected or maxlatency exhausted -> draw */
		timeout = std::chrono::milliseconds(-1);

		if (config::BLINK_TIMEOUT.count() > 0 && m_term.existsBlinkingGlyph()) {
			timeout = config::BLINK_TIMEOUT - blink_watch.elapsed();
			if (timeout.count() <= 0) {
				if (-timeout.count() > config::BLINK_TIMEOUT.count()) /* start visible */
					m_x11.setBlinking(true);
				m_x11.switchBlinking();
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

void Nst::resizeConsole(const Extent &win) {
	m_x11.setWinSize(win);
	const auto &twin = m_x11.termWin();
	auto tdim = twin.getTermDim();

	m_term.resize(tdim);
	m_x11.resize(tdim);
	m_tty.resize(twin.TTYExtent());
}

} // end ns nst

int main(int argc, const char **argv) {
	try {
		nst::Nst nst;
		nst.run(argc, argv);
	} catch (const std::exception &ex) {
		std::cerr << ex.what() << std::endl;
		return EXIT_FAILURE;
	}

	return 0;
}
