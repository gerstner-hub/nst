// C++
#include <algorithm>

// cosmos
#include "cosmos/algs.hxx"
#include "cosmos/formatting.hxx"

// nst
#include "atoms.hxx"
#include "nst_config.hxx"
#include "nst.hxx"
#include "x11.hxx"
#include "XEventHandler.hxx"
#include "XSelection.hxx"

namespace nst {

namespace {

/* XEMBED messages */
constexpr long XEMBED_FOCUS_IN  = 4;
constexpr long XEMBED_FOCUS_OUT = 5;

}

XEventHandler::XEventHandler(Nst &nst) :
		m_nst{nst},
		m_x11{nst.x11()},
		m_xsel{m_x11.selection()},
		m_mouse_shortcuts{config::get_mouse_shortcuts(nst)},
		m_kbd_shortcuts{config::get_kbd_shortcuts(nst)}
{}

void XEventHandler::process(xpp::Event &ev) {
	using Event = xpp::EventType;

	switch(ev.type()) {
		default: return;
		case Event::Ev_KeyPress:         return keyPress(ev.toKeyEvent());
		case Event::Ev_ClientMessage:    return clientMessage(ev.toClientMessage());
		case Event::Ev_ConfigureNotify:  return resize(ev.toConfigureNotify());
		case Event::Ev_VisibilityNotify: return visibilityChange(ev.toVisibilityNotify());
		case Event::Ev_UnmapNotify:      return unmap();
		case Event::Ev_Expose:           return expose();
		case Event::Ev_FocusIn:          /* fallthrough */
		case Event::Ev_FocusOut:         return focus(ev);
		case Event::Ev_MotionNotify:     return motionEvent(ev);
		case Event::Ev_ButtonPress:      return buttonPress(ev.toButtonEvent());
		case Event::Ev_ButtonRelease:    return buttonRelease(ev.toButtonEvent());
		case Event::Ev_SelectionNotify:  return selectionNotify(ev);
		/*
		 * PropertyNotify is only turned on when there is some
		 * INCR transfer happening for the selection retrieval.
		 */
		case Event::Ev_PropertyNotify:   return propertyNotify(ev);
		case Event::Ev_SelectionClear:   return selectionClear();
		case Event::Ev_SelectionRequest: return selectionRequest(ev.toSelectionRequest());
	}
}

bool XEventHandler::stateMatches(unsigned mask, unsigned state) {
	return mask == config::XK_ANY_MOD || mask == (state & ~config::IGNOREMOD);
}

std::optional<std::string_view>
XEventHandler::customKeyMapping(KeySym k, unsigned state) const {
	/* Check for mapped keys out of X11 function keys. */
	const bool found = config::MAPPED_KEYS.count(k) != 0;

	// if the key is not explicitly mapped and it is outside the range of
	// X11 function keys, don't continue
	if (!found && ((k & 0xFFFF) < 0xFD00)) {
		return {};
	}

	const auto &tmode = m_x11.termWin().mode();

	for (auto [it, end] = config::KEYS.equal_range(Key{k}); it != end; it++) {
		auto &key = *it;

		if (!stateMatches(key.mask, state))
			continue;
		if (tmode[WinMode::APPKEYPAD] ? key.appkey < 0 : key.appkey > 0)
			continue;
		if (tmode[WinMode::NUMLOCK] && key.appkey == 2)
			continue;
		if (tmode[WinMode::APPCURSOR] ? key.appcursor < 0 : key.appcursor > 0)
			continue;

		return key.s;
	}

	return {};
}

unsigned XEventHandler::buttonMask(unsigned button) {
	switch(button) {
		default: return 0;
		case Button1: return Button1Mask;
		case Button2: return Button2Mask;
		case Button3: return Button3Mask;
		case Button4: return Button4Mask;
		case Button5: return Button5Mask;
	};
}

bool XEventHandler::handleMouseAction(const XButtonEvent &ev, bool is_release) {
	/* ignore Button<N>mask for Button<N> - it's set on release */
	const unsigned state = ev.state & ~buttonMask(ev.button);

	for (auto &ms: m_mouse_shortcuts) {
		if (ms.release != is_release || ms.button != ev.button)
			continue;

		if ( stateMatches(ms.mod, state) ||  /* exact or forced */
		     stateMatches(ms.mod, state & ~config::FORCE_MOUSE_MOD)) {
			ms.func();
			return true;
		}
	}

	return false;
}

void XEventHandler::handleMouseReport(const XButtonEvent &ev) {
	size_t btn;
	// the escape code to report for the mouse motion
	int code;
	const auto &twin = m_x11.termWin();
	const auto &tmode = twin.mode();
	const auto pos = twin.toCharPos(DrawPos{ev.x, ev.y});

	if (ev.type == MotionNotify) {
		if (pos == m_old_mouse_pos)
			return;
		else if (!tmode[WinMode::MOUSEMOTION] && !tmode[WinMode::MOUSEMANY])
			return;
		/* WinMode::MOUSEMOTION: no reporting if no button is pressed */
		else if (tmode[WinMode::MOUSEMOTION] && m_buttons.none())
			return;
		btn = m_buttons.firstButton();
		code = 32;
	} else {
		btn = ev.button;
		/* Only buttons 1 through 11 can be encoded */
		if (!m_buttons.valid(btn))
			return;
		if (ev.type == ButtonRelease) {
			/* MODE_MOUSEX10: no button release reporting */
			if (tmode[WinMode::MOUSEX10])
				return;
			/* Don't send release events for the scroll wheel */
			else if (m_buttons.isScrollWheel(btn))
				return;
		}
		code = 0;
	}

	m_old_mouse_pos = pos;

	/* Encode btn into code. If no button is pressed for a motion event in
	 * WinMode::MOUSEMANY, then encode it as a release. */
	if ((!tmode[WinMode::MOUSE_SGR] && ev.type == ButtonRelease) || btn == PressedButtons::NO_BUTTON)
		code += 3;
	else if (btn >= 8)
		code += 128 + btn - 8;
	else if (btn >= 4)
		code += 64 + btn - 4;
	else
		code += btn - 1;

	if (!tmode[WinMode::MOUSEX10]) {
		auto state = ev.state;
		code += ((state & ShiftMask  ) ?  4 : 0)
		      + ((state & Mod1Mask   ) ?  8 : 0) /* meta key: alt */
		      + ((state & ControlMask) ? 16 : 0);
	}

	std::string report;

	if (tmode[WinMode::MOUSE_SGR]) {
		const auto ch = ev.type == ButtonRelease ? 'm' : 'M';
		report = cosmos::sprintf(
				"\033[<%d;%d;%d%c",
				code, pos.x+1, pos.y+1, ch);
	} else if (pos.x < 223 && pos.y < 223) {
		report = cosmos::sprintf(
				"\033[M%c%c%c",
				32 + code, 32 + pos.x + 1, 32 + pos.y + 1);
	} else {
		return;
	}

	m_nst.tty().write(report, TTY::MayEcho(false));
}

void XEventHandler::handleMouseSelection(const XButtonEvent &ev, const bool done) {
	auto seltype = Selection::Type::REGULAR;
	const unsigned state = ev.state & ~(Button1Mask | config::FORCE_MOUSE_MOD);

	for (auto [type, mask]: config::SEL_MASKS) {
		if (stateMatches(mask, state)) {
			seltype = type;
			break;
		}
	}

	auto &sel = m_nst.selection();
	const auto pos = m_x11.termWin().toCharPos(DrawPos{ev.x, ev.y});
	sel.extend(pos, seltype, done);

	if (done) {
		auto selection = sel.selection();
		m_xsel.setSelection(selection, ev.time);
	}
}

void XEventHandler::expose() {
	m_nst.term().redraw();
}

void XEventHandler::visibilityChange(const XVisibilityEvent &ev) {
	m_x11.setVisible(ev.state != VisibilityFullyObscured);
}

void XEventHandler::unmap() {
	m_x11.setVisible(false);
}

void XEventHandler::focus(const xpp::Event &ev) {
	if (ev.toFocusChangeEvent().mode == NotifyGrab)
		return;

	m_x11.focusChange(/*in_focus=*/ev.type() == xpp::EventType::Ev_FocusIn);
}

void XEventHandler::keyPress(const XKeyEvent &ev) {
	const auto &tmode = m_x11.termWin().mode();

	if (tmode[WinMode::KBDLOCK])
		return;

	std::string buf;
	const auto ksym = m_x11.m_input.lookupString(ev, buf);

	/* 1. shortcuts */
	for (auto &sc: m_kbd_shortcuts) {
		if (ksym == sc.keysym && stateMatches(sc.mod, ev.state)) {
			sc.func();
			return;
		}
	}

	/* 2. custom keys from nst_config.hxx */
	if (auto seq = customKeyMapping(ksym, ev.state); seq) {
		m_nst.tty().write(*seq, TTY::MayEcho(true));
		return;
	}

	/* 3. composed string from input method */
	if (buf.empty())
		return;

	if (buf.size() == 1 && (ev.state & Mod1Mask)) {
		if (tmode[WinMode::EIGHT_BIT]) {
			if (buf[0] < 0177) {
				Rune c = buf[0] | 0x80;
				buf.clear();
				utf8::encode(c, buf);
			}
		} else {
			buf[1] = buf[0];
			buf[0] = '\033';
			buf.resize(2);
		}
	}

	m_nst.tty().write(buf, TTY::MayEcho(true));
}

void XEventHandler::clientMessage(const XClientMessageEvent &msg) {
	/*
	 * See xembed specs
	 *  http://standards.freedesktop.org/xembed-spec/xembed-spec-latest.html
	 */
	if (xpp::AtomID{msg.message_type} == atoms::xembed && msg.format == 32) {
		switch (msg.data.l[1]) {
			case XEMBED_FOCUS_IN: {
				m_x11.embeddedFocusChange(true);
				break;
			}
			case XEMBED_FOCUS_OUT: {
				m_x11.embeddedFocusChange(false);
				break;
			}
		}
	} else if (xpp::AtomID(msg.data.l[0]) == xpp::atoms::icccm_wm_delete_window) {
		m_nst.tty().hangup();
		exit(0);
	}
}

void XEventHandler::resize(const XConfigureEvent &config) {
	auto new_size = Extent{config.width, config.height};

	if (new_size == m_x11.termWin().winExtent())
		return;

	m_nst.resizeConsole(new_size);
}

void XEventHandler::buttonPress(const XButtonEvent &ev) {
	const auto btn = ev.button;
	const auto &twin = m_x11.termWin();

	if (m_buttons.valid(btn))
		m_buttons.setPressed(btn);

	if (twin.checkFlag(WinMode::MOUSE) && !(ev.state & config::FORCE_MOUSE_MOD)) {
		handleMouseReport(ev);
		return;
	}

	if (handleMouseAction(ev, /*is_release=*/false))
		return;
	else if (btn != Button1)
		return;

	const auto snap = m_xsel.handleClick();
	const auto pos = twin.toCharPos(DrawPos{ev.x, ev.y});
	m_nst.selection().start(pos, snap);
}

void XEventHandler::propertyNotify(const xpp::Event &ev) {
	const auto &prop = ev.toProperty();

	if (prop.state != PropertyNewValue)
	       return;

	const auto prop_id = xpp::AtomID{prop.atom};

	if (prop_id == xpp::atoms::primary_selection || prop_id == xpp::atoms::clipboard) {
		selectionNotify(ev);
	}
}

void XEventHandler::selectionNotify(const xpp::Event &ev) {
	const xpp::AtomID atom = [&ev]() {
		switch (ev.type()) {
			case xpp::EventType::Ev_SelectionNotify:
				return xpp::AtomID{ev.toSelectionNotify().property};
			case xpp::EventType::Ev_PropertyNotify:
				return xpp::AtomID{ev.toProperty().atom};
			default:
				return xpp::AtomID::INVALID;
		}
	}();

	auto &tty = m_nst.tty();
	auto &win = m_x11.window();
	xpp::XWindow::PropertyInfo info;
	xpp::RawProperty prop;

	if (atom == xpp::AtomID::INVALID)
		return;

	prop.length = BUFSIZ;

	do {
		try {
			win.getRawProperty(atom, info, prop);
		} catch (const std::exception &ex) {
			std::cerr << "Clipboard property retrieval failed: " << ex.what() << std::endl;
			return;
		}

		if (ev.isPropertyNotify() && prop.length == 0 && prop.left == 0) {
			/*
			 * If there is some PropertyNotify with no data, then
			 * this is the signal of the selection owner that all
			 * data has been transferred. We won't need to receive
			 * PropertyNotify events anymore.
			 */
			m_x11.changeEventMask(PropertyChangeMask, false);
		}

		if (info.type == atoms::incr) {
			/*
			 * Activate the PropertyNotify events so we receive
			 * when the selection owner does send us the next
			 * chunk of data.
			 */
			m_x11.changeEventMask(PropertyChangeMask, true);

			/*
			 * Deleting the property is the transfer start signal.
			 */
			win.delProperty(atom);
			continue;
		}

		/*
		 * As seen in Selection::selection():
		 * Line endings are inconsistent in the terminal and GUI world
		 * copy and pasting. When receiving some selection data,
		 * replace all '\n' with '\r'.
		 * FIXME: Fix the computer world.
		 */
		auto ptr = prop.data.get();
		std::replace(ptr, ptr + prop.length, '\n', '\r');

		const bool brcktpaste = m_x11.termWin().checkFlag(WinMode::BRKT_PASTE);

		if (brcktpaste && prop.offset == 0)
			tty.write("\033[200~", TTY::MayEcho{false});
		tty.write(std::string_view{reinterpret_cast<const char*>(ptr), prop.length}, TTY::MayEcho{true});
		if (brcktpaste && prop.left == 0)
			tty.write("\033[201~", TTY::MayEcho{false});
		/* number of 32-bit chunks returned */
		prop.offset += prop.length;
	} while (prop.left > 0);

	/*
	 * Deleting the property again tells the selection owner to send the
	 * next data chunk in the property.
	 */
	win.delProperty(atom);
}

void XEventHandler::selectionClear() {
	if (config::SEL_CLEAR) {
		m_nst.selection().clear();
	}
}

void XEventHandler::selectionRequest(const XSelectionRequestEvent &req) {
	XEvent ev;
	XSelectionEvent &xev = ev.xselection;
	xev.type = SelectionNotify;
	xev.requestor = req.requestor;
	xev.selection = req.selection;
	xev.target = req.target;
	xev.time = req.time;
	/* reject by default, if nothing matches below */
	xev.property = None;

	xpp::XWindow requestor{xpp::WinID{req.requestor}};
	const xpp::AtomID target{xpp::AtomID{req.target}};
	const xpp::AtomID req_prop = req.property == None ? target : xpp::AtomID{req.property};

	if (target == atoms::targets) {
		/* respond with the supported type */
		xpp::Property<xpp::AtomID> tgt_format(m_xsel.targetFormat());

		requestor.setProperty(req_prop, tgt_format);
		xev.property = xpp::raw_atom(req_prop);
	} else if (target == m_xsel.targetFormat() || target == xpp::atoms::string_type) {
		/*
		 * with XA_STRING (string_type) non ascii characters may be
		 * incorrect in the requestor. It is not our problem, use
		 * utf8.
		 */
		try {
			auto seltext = m_xsel.getSelection(xpp::AtomID{req.selection});
			if (!seltext.empty()) {
				// TODO: this potentially needlessly copies the
				// selection string, because we need to turn it into
				// an utf8_string wrapper object.
				// either keep utf8_string within XSelection right
				// away or use string_view in utf8_string to avoid
				// this.
				xpp::Property<xpp::utf8_string> sel_utf8{xpp::utf8_string(seltext)};
				requestor.setProperty(req_prop, sel_utf8);
			}
		} catch (const std::exception &ex) {
			std::cerr << "Failed to handle clipboard selection " << cosmos::HexNum{req.selection, 0} << ": " << ex.what() << std::endl;
			return;
		}

		xev.property = xpp::raw_atom(req_prop);
	}

	try {
		/* all done, send a notification to the listener */
		requestor.sendEvent(ev);
	} catch(const std::exception &ex) {
		std::cerr << "Error sending SelectionNotify event: " << ex.what() << std::endl;
	}
}

void XEventHandler::buttonRelease(const XButtonEvent &ev) {
	int btn = ev.button;

	if (m_buttons.valid(btn))
		m_buttons.setReleased(btn);

	const auto &tmode = m_x11.termWin().mode();

	if (tmode[WinMode::MOUSE] && !(ev.state & config::FORCE_MOUSE_MOD)) {
		handleMouseReport(ev);
		return;
	} else if (handleMouseAction(ev, true)) {
		return;
	} else if (btn == Button1) {
		handleMouseSelection(ev, /*done =*/ true);
	}
}

void XEventHandler::motionEvent(const xpp::Event &ev) {
	// NOTE: the code currently exploits the fact that XMotionEvent and
	// XButtonEvent share most of the fields, but the type notion behind
	// this is flawed.
	// avoid the xpp::Event type check from biting us here by accessing the
	// raw structure
	// TODO: maybe fix this using a template function
	const auto &bev = ev.raw()->xbutton;
	const auto &tmode = m_x11.termWin().mode();

	if (tmode[WinMode::MOUSE] && !(bev.state & config::FORCE_MOUSE_MOD)) {
		handleMouseReport(bev);
	} else {
		handleMouseSelection(bev);
	}
}

} // end ns
