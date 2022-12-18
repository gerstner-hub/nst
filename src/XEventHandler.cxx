// cosmos
#include "cosmos/algs.hxx"

// nst
#include "nst.hxx"
#include "nst_config.hxx"
#include "XEventHandler.hxx"
#include "XSelection.hxx"
#include "x11.hxx"

namespace nst {

XEventHandler::XEventHandler(Nst &nst) :
	m_nst(nst),
	m_x11(nst.getX11()),
	m_xsel(m_x11.getXSelection()),
	m_mouse_shortcuts(config::getMouseShortcuts(nst)),
	m_kbd_shortcuts(config::getKbdShortcuts(nst))
{}

void XEventHandler::init() {
	m_xembed_atom = m_x11.getAtom("_XEMBED");
}

bool XEventHandler::match(uint mask, uint state) {
	return mask == XK_ANY_MOD || mask == (state & ~config::IGNOREMOD);
}

const char* XEventHandler::getCustomKey(KeySym k, uint state) const {
	/* Check for mapped keys out of X11 function keys. */
	const bool found = config::MAPPEDKEYS.count(k) != 0;

	// if the key is not explicitly mapped and it is outside the range of
	// X11 function keys, don't continue
	if (!found && ((k & 0xFFFF) < 0xFD00)) {
		return nullptr;
	}

	const auto &tmode = m_x11.getTermWin().mode;

	for (auto [it, end] = config::KEYS.equal_range(Key{k}); it != end; it++) {
		auto &key = *it;

		if (!match(key.mask, state))
			continue;

		if (tmode[WinMode::APPKEYPAD] ? key.appkey < 0 : key.appkey > 0)
			continue;
		if (tmode[WinMode::NUMLOCK] && key.appkey == 2)
			continue;

		if (tmode[WinMode::APPCURSOR] ? key.appcursor < 0 : key.appcursor > 0)
			continue;

		return key.s;
	}

	return nullptr;
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

		if ( match(ms.mod, state) ||  /* exact or forced */
		     match(ms.mod, state & ~config::FORCEMOUSEMOD)) {
			ms.func();
			return true;
		}
	}

	return false;
}

void XEventHandler::handleMouseReport(const XButtonEvent &ev) {
	size_t btn;
	int code;
	const auto &twin = m_x11.getTermWin();
	const auto &tmode = twin.mode;
	const auto pos = twin.getCharPos(DrawPos{ev.x, ev.y});

	if (ev.type == MotionNotify) {
		if (pos == m_old_mouse_pos)
			return;
		else if (!tmode[WinMode::MOUSEMOTION] && !tmode[WinMode::MOUSEMANY])
			return;
		/* MODE_MOUSEMOTION: no reporting if no button is pressed */
		else if (tmode[WinMode::MOUSEMOTION] && m_buttons.none())
			return;
		/* Set btn to lowest-numbered pressed button, or NO_BUTTON if no
		 * buttons are pressed. */
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
			if (btn == 4 || btn == 5)
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

	int len;
	char buf[40];

	if (tmode[WinMode::MOUSESGR]) {
		len = snprintf(buf, sizeof(buf), "\033[<%d;%d;%d%c",
				code, pos.x+1, pos.y+1,
				ev.type == ButtonRelease ? 'm' : 'M');
	} else if (pos.x < 223 && pos.y < 223) {
		len = snprintf(buf, sizeof(buf), "\033[M%c%c%c",
				32+code, 32+pos.x+1, 32+pos.y+1);
	} else {
		return;
	}

	if (len >= 0) {
		m_nst.getTTY().write(buf, len, false);
	}
}

void XEventHandler::handleMouseSelection(const XButtonEvent &ev, bool done) {
	auto seltype = Selection::Type::REGULAR;
	const uint state = ev.state & ~(Button1Mask | config::FORCEMOUSEMOD);

	for (auto [type, mask]: config::SELMASKS) {
		if (match(mask, state)) {
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

void XEventHandler::visibility(const XVisibilityEvent &ev) {
	m_x11.setVisible(ev.state != VisibilityFullyObscured);
}

void XEventHandler::unmap() {
	m_x11.setVisible(false);
}

void XEventHandler::focus(const xpp::Event &ev) {

	if (ev.toFocusChangeEvent().mode == NotifyGrab)
		return;

	if (ev.getType() == FocusIn) {
		m_x11.focusChange(true);
	} else {
		m_x11.focusChange(false);
	}
}

void XEventHandler::kpress(const XKeyEvent &ev) {
	KeySym ksym;
	char buf[64];
	int len;

	const auto &tmode = m_x11.getTermWin().mode;

	if (tmode[WinMode::KBDLOCK])
		return;

	auto &input = m_x11.getInput();

	if (input.haveContext()) {
		Status status;
		len = XmbLookupString(input.getContext(), const_cast<XKeyEvent*>(&ev), buf, sizeof(buf), &ksym, &status);
	}
	else
		len = XLookupString(const_cast<XKeyEvent*>(&ev), buf, sizeof(buf), &ksym, NULL);

	/* 1. shortcuts */
	for (auto &sc: m_kbd_shortcuts) {
		if (ksym == sc.keysym && match(sc.mod, ev.state)) {
			sc.func();
			return;
		}
	}

	/* 2. custom keys from nst_config.h */
	if (const char *customkey = getCustomKey(ksym, ev.state); customkey != nullptr) {
		m_nst.getTTY().write(customkey, strlen(customkey), 1);
		return;
	}

	/* 3. composed string from input method */
	if (len == 0)
		return;

	if (len == 1 && (ev.state & Mod1Mask)) {
		if (tmode[WinMode::EIGHT_BIT]) {
			if (*buf < 0177) {
				Rune c = *buf | 0x80;
				len = utf8::encode(c, buf);
			}
		} else {
			buf[1] = buf[0];
			buf[0] = '\033';
			len = 2;
		}
	}

	m_nst.getTTY().write(buf, len, 1);
}

void XEventHandler::cmessage(const XClientMessageEvent &msg) {
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
	} else if ((Atom)msg.data.l[0] == m_x11.getWmDeleteWin()) {
		m_nst.getTTY().hangup();
		exit(0);
	}
}

void XEventHandler::resize(const XConfigureEvent &config) {
	auto new_size = Extent{config.width, config.height};

	if (new_size == m_x11.getTermWin().win)
		return;

	m_nst.resizeConsole(new_size);
}

void XEventHandler::bpress(const XButtonEvent &ev) {
	const auto btn = ev.button;
	const auto &twin = m_x11.getTermWin();

	if (m_buttons.valid(btn))
		m_buttons.setPressed(btn);


	if (twin.mode[WinMode::MOUSE] && !(ev.state & config::FORCEMOUSEMOD)) {
		handleMouseReport(ev);
		return;
	}

	if (handleMouseAction(ev, false))
		return;

	if (btn != Button1)
		return;

	const auto snap = m_xsel.handleClick();
	auto &selection = m_nst.getSelection();
	const auto pos = twin.getCharPos(DrawPos{ev.x, ev.y});
	selection.start(pos.x, pos.y, snap);
}

void XEventHandler::propnotify(const xpp::Event &ev) {
	const auto &prop = ev.toProperty();
	Atom clipboard = m_x11.getAtom("CLIPBOARD");

	if (prop.state == PropertyNewValue &&
			cosmos::in_list(prop.atom, {XA_PRIMARY, clipboard})) {
		selnotify(ev);
	}
}

void XEventHandler::selnotify(const xpp::Event &ev) {
	const Atom property = [&ev]() {
		switch (ev.getType()) {
			case SelectionNotify: return ev.toSelectionNotify().property;
			case PropertyNotify: return ev.toProperty().atom;
			default: return (Atom)None;
		}
	}();
	const Atom incratom = m_x11.getAtom("INCR");
	ulong nitems, rem, ofs = 0;
	uchar *data, *last;
	Atom type;
	int format;
	auto &tty = m_nst.getTTY();

	if (property == None)
		return;

	do {
		if (XGetWindowProperty(m_x11.getDisplay(), m_x11.getWindow(), property, ofs,
					BUFSIZ/4, False, AnyPropertyType,
					&type, &format, &nitems, &rem,
					&data)) {
			fprintf(stderr, "Clipboard allocation failed\n");
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

		if (type == incratom) {
			/*
			 * Activate the PropertyNotify events so we receive
			 * when the selection owner does send us the next
			 * chunk of data.
			 */
			m_x11.changeEventMask(PropertyChangeMask, true);

			/*
			 * Deleting the property is the transfer start signal.
			 */
			XDeleteProperty(m_x11.getDisplay(), m_x11.getWindow(), (int)property);
			continue;
		}

		/*
		 * As seen in Selection::getSelection():
		 * Line endings are inconsistent in the terminal and GUI world
		 * copy and pasting. When receiving some selection data,
		 * replace all '\n' with '\r'.
		 * FIXME: Fix the computer world.
		 */
		uchar *repl = data;
		last = data + nitems * format / 8;
		while ((repl = (uchar*)memchr(repl, '\n', last - repl))) {
			*repl++ = '\r';
		}

		const auto &tmode = m_x11.getTermWin().mode;

		if (tmode[WinMode::BRCKTPASTE] && ofs == 0)
			tty.write("\033[200~", 6, 0);
		tty.write((char *)data, nitems * format / 8, 1);
		if (tmode[WinMode::BRCKTPASTE] && rem == 0)
			tty.write("\033[201~", 6, 0);
		XFree(data);
		/* number of 32-bit chunks returned */
		ofs += nitems * format / 32;
	} while (rem > 0);

	/*
	 * Deleting the property again tells the selection owner to send the
	 * next data chunk in the property.
	 */
	XDeleteProperty(m_x11.getDisplay(), m_x11.getWindow(), (int)property);
}

[[maybe_unused]]
void XEventHandler::selclear() {
	m_nst.getSelection().clear();
}

void XEventHandler::selrequest(const XSelectionRequestEvent &req) {
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
		Atom string = m_xsel.getTargetFormat();
		XChangeProperty(req.display, req.requestor, reqprop,
				XA_ATOM, 32, PropModeReplace,
				(uchar *) &string, 1);
		xev.property = reqprop;
	} else if (req.target == m_xsel.getTargetFormat() || req.target == XA_STRING) {
		/*
		 * with XA_STRING non ascii characters may be incorrect in the
		 * requestor. It is not our problem, use utf8.
		 */
		auto seltext = m_xsel.getSelection(req.selection);

		if (!seltext) {
			fprintf(stderr,
				"Unhandled clipboard selection 0x%lx\n",
				req.selection);
			return;
		}

		if (!seltext->empty()) {
			XChangeProperty(req.display, req.requestor,
					reqprop, req.target,
					8, PropModeReplace,
					(uchar *)seltext->c_str(), seltext->size());
			xev.property = reqprop;
		}
	}

	/* all done, send a notification to the listener */
	if (!XSendEvent(req.display, req.requestor, 1, 0, (XEvent *) &xev))
		fprintf(stderr, "Error sending SelectionNotify event\n");
}

void XEventHandler::brelease(const XButtonEvent &ev) {
	int btn = ev.button;

	if (m_buttons.valid(btn))
		m_buttons.setReleased(btn);

	const auto &tmode = m_x11.getTermWin().mode;

	if (tmode[WinMode::MOUSE] && !(ev.state & config::FORCEMOUSEMOD)) {
		handleMouseReport(ev);
		return;
	}

	if (handleMouseAction(ev, true))
		return;

	if (btn == Button1)
		handleMouseSelection(ev, /*done =*/ true);
}

void XEventHandler::bmotion(const xpp::Event &ev) {
	// NOTE: the code currently exploits the fact that XMotionEvent and
	// XButtonEvent share most of the fields, but the type notion behind
	// this is flawed.
	// avoid the xpp::Event type check to bite us here by accessing the
	// raw structure
	const auto &bev = ev.raw()->xbutton;

	const auto &tmode = m_x11.getTermWin().mode;

	if (tmode[WinMode::MOUSE] && !(bev.state & config::FORCEMOUSEMOD)) {
		handleMouseReport(bev);
	}
	else {
		handleMouseSelection(bev);
	}
}

} // end ns
