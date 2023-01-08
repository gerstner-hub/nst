// cosmos
#include "cosmos/io/Poller.hxx"
#include "cosmos/time/TimeSpec.hxx"

// nst
#include "nst.hxx"

namespace nst {

Nst::Nst() :
		m_x11(*this),
		m_term(*this),
		m_tty(m_term),
		m_selection(*this),
		m_event_handler(*this)
{}

void Nst::waitForWindowMapping() {
	xpp::Event ev;
	auto &display = m_x11.getDisplay();

	/* Waiting for window mapping */
	do {
		display.getNextEvent(ev);
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

	resizeConsole(m_x11.getTermWin().getWinExtent());
}

void Nst::run(int argc, const char **argv) {
	setlocale(LC_CTYPE, "");
	XSetLocaleModifiers("");

	m_cmdline.parse(argc, argv);
	m_term.init(m_x11.getTermSize());
	m_x11.init();
	m_event_handler.init();
	setEnv();
	mainLoop();
}

void Nst::setEnv() {
	::setenv("WINDOWID", std::to_string(m_x11.getWindow()).c_str(), 1);
}


void Nst::mainLoop() {
	auto ttyfd = m_tty.create(m_cmdline);
	auto childfd = m_tty.getChildFD();
	auto &display = m_x11.getDisplay();
	auto xfd = display.getConnectionNumber();

	cosmos::Poller poller;
	poller.create();
	for (auto fd: {ttyfd, xfd, childfd}) {
		poller.addFD(fd, cosmos::Poller::MonitorMask({cosmos::Poller::MonitorSetting::INPUT}));
	}

	xpp::Event ev;
	bool drawing = false;
	cosmos::MonotonicStopWatch draw_watch;
	cosmos::MonotonicStopWatch blink_watch(cosmos::MonotonicStopWatch::InitialMark(true));
	std::chrono::milliseconds timeout(-1);

	waitForWindowMapping();

	while (true) {
		if (display.hasPendingEvents())
			timeout = std::chrono::milliseconds(0);  /* existing events might not set xfd */

		auto events = poller.wait(timeout.count() >= 0 ?
				std::optional<std::chrono::milliseconds>(timeout) :
				std::nullopt);

		bool draw_event = false;

		for (const auto &event: events) {
			if (event.fd() == childfd) {
				m_tty.sigChildEvent();
			} else if (event.fd() == ttyfd) {
				m_tty.read();
				draw_event = true;
			}
		}

		while (display.hasPendingEvents()) {
			draw_event = true;
			display.getNextEvent(ev);
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
		 * and eventually draw even without idle after MAXLATENCY ms.
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
			timeout = (config::MAXLATENCY - diff) / config::MAXLATENCY * config::MINLATENCY;

			if (timeout.count() > 0)
				continue;  /* we have time, try to find idle */
		}

		/* idle detected or maxlatency exhausted -> draw */
		timeout = std::chrono::milliseconds(-1);

		if (config::BLINKTIMEOUT.count() > 0 && m_term.existsBlinkingGlyph()) {
			timeout = config::BLINKTIMEOUT - blink_watch.elapsed();
			if (timeout.count() <= 0) {
				if (-timeout.count() > config::BLINKTIMEOUT.count()) /* start visible */
					m_x11.setBlinking(true);
				m_x11.switchBlinking();
				m_term.setDirtyByAttr(Attr::BLINK);
				blink_watch.mark();
				timeout = config::BLINKTIMEOUT;
			}
		}

		m_term.draw();
		display.flush();
		drawing = false;
	}
}

void Nst::resizeConsole(const Extent &win) {
	m_x11.setWinSize(win);
	const auto &twin = m_x11.getTermWin();
	auto tdim = twin.getTermDim();

	m_term.resize(tdim);
	m_x11.resize(tdim);
	m_tty.resize(twin.getTTYExtent());
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
