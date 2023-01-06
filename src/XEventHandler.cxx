// cosmos
#include "cosmos/algs.hxx"
#include "cosmos/formatting.hxx"

// nst
#include "nst.hxx"
#include "nst_config.hxx"
#include "XEventHandler.hxx"
#include "XSelection.hxx"
#include "x11.hxx"

namespace nst {

namespace {

/* XEMBED messages */
constexpr long XEMBED_FOCUS_IN  = 4;
constexpr long XEMBED_FOCUS_OUT = 5;

}

XEventHandler::XEventHandler(Nst &nst) :
	m_nst(nst),
	m_x11(nst.getX11()),
	m_xsel(m_x11.getXSelection()),
	m_mouse_shortcuts(config::getMouseShortcuts(nst)),
	m_kbd_shortcuts(config::getKbdShortcuts(nst))
{}

void XEventHandler::init() {
	m_xembed_atom = m_x11.getXAtom("_XEMBED");
	m_incr_atom = m_x11.getXAtom("INCR");
}

void XEventHandler::process(xpp::Event &ev) {
	switch(ev.getType()) {
		case KeyPress:         return keyPress(ev.toKeyEvent());
		case ClientMessage:    return clientMessage(ev.toClientMessage());
		case ConfigureNotify:  return resize(ev.toConfigureNotify());
		case VisibilityNotify: return visibilityChange(ev.toVisibilityNotify());
		case UnmapNotify:      return unmap();
		case Expose:           return expose();
		case FocusIn:          /* fallthrough */
		case FocusOut:         return focus(ev);
		case MotionNotify:     return motionEvent(ev);
		case ButtonPress:      return buttonPress(ev.toButtonEvent());
		case ButtonRelease:    return buttonRelease(ev.toButtonEvent());
		case SelectionNotify:  return selectionNotify(ev);
		/*
		 * PropertyNotify is only turned on when there is some
		 * INCR transfer happening for the selection retrieval.
		 */
		case PropertyNotify:   return propertyNotify(ev);
		case SelectionClear:   return selectionClear();
		case SelectionRequest: return selectionRequest(ev.toSelectionRequest());
	}
}

bool XEventHandler::stateMatches(unsigned mask, unsigned state) {
	return mask == XK_ANY_MOD || mask == (state & ~config::IGNOREMOD);
}

std::optional<std::string_view>
XEventHandler::getCustomKeyMapping(KeySym k, unsigned state) const {
	/* Check for mapped keys out of X11 function keys. */
	const bool found = config::MAPPEDKEYS.count(k) != 0;

	// if the key is not explicitly mapped and it is outside the range of
	// X11 function keys, don't continue
	if (!found && ((k & 0xFFFF) < 0xFD00)) {
		return {};
	}

	const auto &tmode = m_x11.getTermWin().getMode();

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

unsigned XEventHandler::getButtonMask(unsigned button) {
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
	const unsigned state = ev.state & ~getButtonMask(ev.button);

	for (auto &ms: m_mouse_shortcuts) {
		if (ms.release != is_release || ms.button != ev.button)
			continue;

		if ( stateMatches(ms.mod, state) ||  /* exact or forced */
		     stateMatches(ms.mod, state & ~config::FORCEMOUSEMOD)) {
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
	const auto &twin = m_x11.getTermWin();
	const auto &tmode = twin.getMode();
	const auto pos = twin.getCharPos(DrawPos{ev.x, ev.y});

	if (ev.type == MotionNotify) {
		if (pos == m_old_mouse_pos)
			return;
		else if (!tmode[WinMode::MOUSEMOTION] && !tmode[WinMode::MOUSEMANY])
			return;
		/* MODE_MOUSEMOTION: no reporting if no button is pressed */
		else if (tmode[WinMode::MOUSEMOTION] && m_buttons.none())
			return;
		btn = m_buttons.getFirstButton();
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
	 * MODE_MOUSEMANY, then encode it as a release. */
	if ((!tmode[WinMode::MOUSESGR] && ev.type == ButtonRelease) || btn == PressedButtons::NO_BUTTON)
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

	if (tmode[WinMode::MOUSESGR]) {
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

	m_nst.getTTY().write(report.c_str(), report.size(), false);
}

void XEventHandler::handleMouseSelection(const XButtonEvent &ev, const bool done) {
	auto seltype = Selection::Type::REGULAR;
	const unsigned state = ev.state & ~(Button1Mask | config::FORCEMOUSEMOD);

	for (auto [type, mask]: config::SELMASKS) {
		if (stateMatches(mask, state)) {
			seltype = type;
			break;
		}
	}

	auto &sel = m_nst.getSelection();
	const auto pos = m_x11.getTermWin().getCharPos(DrawPos{ev.x, ev.y});
	sel.extend(pos.x, pos.y, seltype, done);

	if (done) {
		auto selection = sel.getSelection();
		m_xsel.setSelection(selection, ev.time);
	}
}

void XEventHandler::expose() {
	m_nst.getTerm().redraw();
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

	m_x11.focusChange(/*in_focus=*/ev.getType() == FocusIn);
}

void XEventHandler::keyPress(const XKeyEvent &ev) {
	const auto &tmode = m_x11.getTermWin().getMode();

	if (tmode[WinMode::KBDLOCK])
		return;

	std::string buf;
	const auto ksym = m_x11.getInput().lookupString(ev, buf);

	/* 1. shortcuts */
	for (auto &sc: m_kbd_shortcuts) {
		if (ksym == sc.keysym && stateMatches(sc.mod, ev.state)) {
			sc.func();
			return;
		}
	}

	/* 2. custom keys from nst_config.hxx */
	if (auto seq = getCustomKeyMapping(ksym, ev.state); seq) {
		m_nst.getTTY().write(seq->data(), seq->size(), /*may_echo=*/true);
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

	m_nst.getTTY().write(buf.c_str(), buf.size(), /*may_echo=*/true);
}

void XEventHandler::clientMessage(const XClientMessageEvent &msg) {
	/*
	 * See xembed specs
	 *  http://standards.freedesktop.org/xembed-spec/xembed-spec-latest.html
	 */
	if (msg.message_type == m_xembed_atom && msg.format == 32) {
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
	} else if (xpp::XAtom(msg.data.l[0]) == m_x11.getWmDeleteWin()) {
		m_nst.getTTY().hangup();
		exit(0);
	}
}

void XEventHandler::resize(const XConfigureEvent &config) {
	auto new_size = Extent{config.width, config.height};

	if (new_size == m_x11.getTermWin().getWinExtent())
		return;

	m_nst.resizeConsole(new_size);
}

void XEventHandler::buttonPress(const XButtonEvent &ev) {
	const auto btn = ev.button;
	const auto &twin = m_x11.getTermWin();

	if (m_buttons.valid(btn))
		m_buttons.setPressed(btn);

	if (twin.checkFlag(WinMode::MOUSE) && !(ev.state & config::FORCEMOUSEMOD)) {
		handleMouseReport(ev);
		return;
	}

	if (handleMouseAction(ev, /*is_release=*/false))
		return;
	else if (btn != Button1)
		return;

	const auto snap = m_xsel.handleClick();
	const auto pos = twin.getCharPos(DrawPos{ev.x, ev.y});
	m_nst.getSelection().start(pos.x, pos.y, snap);
}

void XEventHandler::propertyNotify(const xpp::Event &ev) {
	const auto &prop = ev.toProperty();
	Atom clipboard = m_x11.getAtom("CLIPBOARD");

	if (prop.state == PropertyNewValue && cosmos::in_list(prop.atom, {XA_PRIMARY, clipboard})) {
		selectionNotify(ev);
	}
}

void XEventHandler::selectionNotify(const xpp::Event &ev) {
	const Atom property = [&ev]() {
		switch (ev.getType()) {
			case SelectionNotify: return ev.toSelectionNotify().property;
			case PropertyNotify: return ev.toProperty().atom;
			default: return (Atom)None;
		}
	}();
	unsigned long nitems, rem, ofs = 0;
	unsigned char *data;
	Atom type;
	int format;
	auto &tty = m_nst.getTTY();
	auto &win = m_x11.getWindow();

	if (property == None)
		return;

	do {
		if (XGetWindowProperty(m_x11.getDisplay(), m_x11.getWindow(), property, ofs,
					BUFSIZ/4, False, AnyPropertyType,
					&type, &format, &nitems, &rem, &data)) {
			std::cerr << "Clipboard allocation failed" << std::endl;
			return;
		}

		if (ev.isPropertyNotify() && nitems == 0 && rem == 0) {
			/*
			 * If there is some PropertyNotify with no data, then
			 * this is the signal of the selection owner that all
			 * data has been transferred. We won't need to receive
			 * PropertyNotify events anymore.
			 */
			m_x11.changeEventMask(PropertyChangeMask, false);
		}

		if (type == m_incr_atom) {
			/*
			 * Activate the PropertyNotify events so we receive
			 * when the selection owner does send us the next
			 * chunk of data.
			 */
			m_x11.changeEventMask(PropertyChangeMask, true);

			/*
			 * Deleting the property is the transfer start signal.
			 */
			win.delProperty(property);
			continue;
		}

		/*
		 * As seen in Selection::getSelection():
		 * Line endings are inconsistent in the terminal and GUI world
		 * copy and pasting. When receiving some selection data,
		 * replace all '\n' with '\r'.
		 * FIXME: Fix the computer world.
		 */
		unsigned char *needle = data;
		unsigned char *last = data + nitems * format / 8;
		while ((needle = (unsigned char*)memchr(needle, '\n', last - needle))) {
			*needle++ = '\r';
		}

		const bool brcktpaste = m_x11.getTermWin().checkFlag(WinMode::BRCKTPASTE);

		if (brcktpaste && ofs == 0)
			tty.write("\033[200~", 6, false);
		tty.write((char *)data, nitems * format / 8, true);
		if (brcktpaste && rem == 0)
			tty.write("\033[201~", 6, false);
		XFree(data);
		/* number of 32-bit chunks returned */
		ofs += nitems * format / 32;
	} while (rem > 0);

	/*
	 * Deleting the property again tells the selection owner to send the
	 * next data chunk in the property.
	 */
	win.delProperty(property);
}

void XEventHandler::selectionClear() {
	if (config::SELCLEAR) {
		m_nst.getSelection().clear();
	}
}

void XEventHandler::selectionRequest(const XSelectionRequestEvent &req) {
	XSelectionEvent xev;
	xev.type = SelectionNotify;
	xev.requestor = req.requestor;
	xev.selection = req.selection;
	xev.target = req.target;
	xev.time = req.time;
	/* reject */
	xev.property = None;

	const auto reqprop = req.property == None ? req.target : req.property;

	if (req.target == m_x11.getAtom("TARGETS")) {
		/* respond with the supported type */
		Atom fmt = m_xsel.getTargetFormat();
		XChangeProperty(req.display, req.requestor, reqprop,
				XA_ATOM, 32, PropModeReplace,
				(unsigned char *) &fmt, 1);
		xev.property = reqprop;
	} else if (req.target == m_xsel.getTargetFormat() || req.target == XA_STRING) {
		/*
		 * with XA_STRING non ascii characters may be incorrect in the
		 * requestor. It is not our problem, use utf8.
		 */
		auto seltext = m_xsel.getSelection(req.selection);

		if (!seltext) {
			std::cerr << "Unhandled clipboard selection " << cosmos::hexnum(req.selection, 0) << std::endl;
			return;
		} else if (!seltext->empty()) {
			XChangeProperty(req.display, req.requestor,
					reqprop, req.target,
					8, PropModeReplace,
					(unsigned char *)seltext->c_str(), seltext->size());
			xev.property = reqprop;
		}
	}

	/* all done, send a notification to the listener */
	if (!XSendEvent(req.display, req.requestor, 1, 0, (XEvent *) &xev))
		std::cerr << "Error sending SelectionNotify event" << std::endl;
}

void XEventHandler::buttonRelease(const XButtonEvent &ev) {
	int btn = ev.button;

	if (m_buttons.valid(btn))
		m_buttons.setReleased(btn);

	const auto &tmode = m_x11.getTermWin().getMode();

	if (tmode[WinMode::MOUSE] && !(ev.state & config::FORCEMOUSEMOD)) {
		handleMouseReport(ev);
		return;
	} else if (handleMouseAction(ev, true))
		return;
	else if (btn == Button1)
		handleMouseSelection(ev, /*done =*/ true);
}

void XEventHandler::motionEvent(const xpp::Event &ev) {
	// NOTE: the code currently exploits the fact that XMotionEvent and
	// XButtonEvent share most of the fields, but the type notion behind
	// this is flawed.
	// avoid the xpp::Event type check from biting us here by accessing the
	// raw structure
	// TODO: maybe fix this using a template function
	const auto &bev = ev.raw()->xbutton;
	const auto &tmode = m_x11.getTermWin().getMode();

	if (tmode[WinMode::MOUSE] && !(bev.state & config::FORCEMOUSEMOD)) {
		handleMouseReport(bev);
	}
	else {
		handleMouseSelection(bev);
	}
}

} // end ns
