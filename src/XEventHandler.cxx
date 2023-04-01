// C++
#include <iostream>
#include <algorithm>

// cosmos
#include "cosmos/algs.hxx"
#include "cosmos/formatting.hxx"

// X++
#include "X++/atoms.hxx"
#include "X++/event/ButtonEvent.hxx"
#include "X++/event/ClientMessageEvent.hxx"
#include "X++/event/ConfigureEvent.hxx"
#include "X++/event/FocusChangeEvent.hxx"
#include "X++/event/KeyEvent.hxx"
#include "X++/event/PointerMovedEvent.hxx"
#include "X++/event/PropertyEvent.hxx"
#include "X++/event/SelectionEvent.hxx"
#include "X++/event/SelectionRequestEvent.hxx"
#include "X++/event/VisibilityEvent.hxx"
#include "X++/formatting.hxx"
#include "X++/Property.hxx"

// nst
#include "atoms.hxx"
#include "nst_config.hxx"
#include "nst.hxx"
#include "x11.hxx"
#include "XEventHandler.hxx"
#include "XSelection.hxx"

namespace nst {

namespace {

	/// xembed protocol message types
	enum class XEmbedMessage : long {
		FOCUS_IN = 4,
		FOCUS_OUT = 5
	};

	bool state_matches(const xpp::InputMask mask, const xpp::InputMask state) {
		return mask[xpp::InputModifier::ANY] || mask == (state - config::IGNOREMOD);
	}

	xpp::InputModifier button_mask(xpp::Button button) {
		using xpp::InputModifier;
		using xpp::Button;
		switch(button) {
			default: return InputModifier{0};
			case Button::BUTTON1: return InputModifier::BUTTON1;
			case Button::BUTTON2: return InputModifier::BUTTON2;
			case Button::BUTTON3: return InputModifier::BUTTON3;
			case Button::BUTTON4: return InputModifier::BUTTON4;
			case Button::BUTTON5: return InputModifier::BUTTON5;
		};
	}

} // end anon ns

XEventHandler::XEventHandler(Nst &nst) :
		m_nst{nst},
		m_x11{nst.x11()},
		m_twin{m_x11.termWin()},
		m_mouse_shortcuts{config::get_mouse_shortcuts(nst)},
		m_kbd_shortcuts{config::get_kbd_shortcuts(nst)}
{}

bool XEventHandler::checkEvents() {
	auto &display = xpp::display;
	bool ret = false;

	while (display.hasPendingEvents()) {
		display.nextEvent(m_event);
		ret = true;
		if (!m_event.filterEvent()) {
			process();
		}
	}

	return ret;
}

void XEventHandler::process() {
	using Event = xpp::EventType;

	switch(m_event.type()) {
		default: return;
		case Event::KEY_PRESS:         return keyPress(xpp::KeyEvent{m_event});
		case Event::CLIENT_MESSAGE:    return clientMessage(xpp::ClientMessageEvent{m_event});
		case Event::CONFIGURE_NOTIFY:  return resize(xpp::ConfigureEvent{m_event});
		case Event::VISIBILITY_NOTIFY: return visibilityChange(xpp::VisibilityEvent{m_event});
		case Event::UNMAP_NOTIFY:      return unmap();
		case Event::EXPOSE:            return expose();
		case Event::FOCUS_IN:          /* fallthrough */
		case Event::FOCUS_OUT:         return focus(xpp::FocusChangeEvent{m_event});
		case Event::MOTION_NOTIFY:     return pointerMovedEvent(xpp::PointerMovedEvent{m_event});
		case Event::BUTTON_PRESS:      return buttonPress(xpp::ButtonEvent{m_event});
		case Event::BUTTON_RELEASE:    return buttonRelease(xpp::ButtonEvent{m_event});
		case Event::SELECTION_NOTIFY:  return selectionNotify(xpp::SelectionEvent{m_event});
		case Event::PROPERTY_NOTIFY:   return propertyNotify(xpp::PropertyEvent{m_event});
		case Event::SELECTION_CLEAR:   return selectionClear();
		case Event::SELECTION_REQUEST: return selectionRequest(xpp::SelectionRequestEvent{m_event});
	}
}

bool XEventHandler::handleMouseAction(const xpp::ButtonEvent &ev) {
	// ignore Button<N>mask for Button<N> - it's set on release
	const auto state = ev.state() - button_mask(ev.buttonNr());
	const bool is_release = ev.type() == xpp::EventType::BUTTON_RELEASE;
	const auto forced_state = state - config::FORCE_MOUSE_MOD;

	for (auto &ms: m_mouse_shortcuts) {
		if (ms.release != is_release || ms.button != ev.buttonNr())
			continue;

		// exact or forced match
		if (state_matches(ms.mod, state) || state_matches(ms.mod, forced_state)) {
			ms.func();
			return true;
		}
	}

	return false;
}

bool XEventHandler::checkMouseReport(const xpp::PointerMovedEvent &ev,
		xpp::Button &button, int &code) {
	const auto pos = m_twin.toCharPos(DrawPos{ev.pos()});

	if (pos == m_old_mouse_pos) {
		return false;
	} else if (!m_twin.reportMouseMotion() && !m_twin.reportMouseMany()) {
		return false;
	} else if (m_twin.reportMouseMotion() && m_buttons.none()) {
		// FIXME: doesn't this mean that MOUSEMANY won't work
		// if reportMouseMotion() is also set and no button is pressed?
		// WinMode::MOUSEMOTION: no reporting if no button is pressed
		return false;
	}

	button = m_buttons.firstButton();
	code = 32;
	m_old_mouse_pos = pos;
	return true;
}

bool XEventHandler::checkMouseReport(const xpp::ButtonEvent &ev, xpp::Button &button, int &code) {
	const auto pos = m_twin.toCharPos(DrawPos{ev.pos()});

	button = ev.buttonNr();
	// Only buttons 1 through 11 can be encoded.
	if (!m_buttons.valid(button)) {
		return false;
	} else if (ev.type() == xpp::EventType::BUTTON_RELEASE) {
		if (m_twin.doX10Compatibility()) {
			// MODE_MOUSEX10: no button release reporting.
			return false;
		} else if (m_buttons.isScrollWheel(button)) {
			// Don't send release events for the scroll wheel.
			return false;
		}
	}

	code = 0;
	m_old_mouse_pos = pos;

	return true;
}

template <typename EVENT>
void XEventHandler::handleMouseReport(const EVENT &ev) {
	xpp::Button button;
	int code; // the escape code to report for the mouse motion

	if (!checkMouseReport(ev, button, code)) {
		return;
	}

	const bool is_release = ev.type() == xpp::EventType::BUTTON_RELEASE;
	const bool report_sgr = m_twin.reportMouseSGR();
	const auto pos = m_old_mouse_pos;

	// Encode button into a report code.
	// If no button is pressed for a motion event in WinMode::MOUSEMANY, then encode it as a release.
	if ((!report_sgr && is_release) ||
			button == PressedButtons::NO_BUTTON)
		code += 3;
	else if (button >= xpp::Button{8})
		code += 128 + xpp::raw_button(button) - 8;
	else if (button >= xpp::Button{4})
		code += 64 + xpp::raw_button(button) - 4;
	else
		code += xpp::raw_button(button) - 1;

	using xpp::InputModifier;

	if (!m_twin.doX10Compatibility()) {
		const auto state = ev.state();
		code += (state[InputModifier::SHIFT]   ?  4 : 0)
		      + (state[InputModifier::MOD1]    ?  8 : 0) // meta key: alt
		      + (state[InputModifier::CONTROL] ? 16 : 0);
	}

	std::string report;

	if (report_sgr) {
		const auto ch = is_release ? 'm' : 'M';
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

template <typename EVENT>
void XEventHandler::handleMouseSelection(const EVENT &ev, const bool done) {
	auto seltype = Selection::Type::REGULAR;
	const auto state = (ev.state() - xpp::InputModifier::BUTTON1) - config::FORCE_MOUSE_MOD;

	for (auto [type, mask]: config::SEL_MASKS) {
		if (state_matches(mask, state)) {
			seltype = type;
			break;
		}
	}

	auto &sel = m_nst.selection();
	const auto pos = m_twin.toCharPos(DrawPos{ev.pos()});
	sel.extend(pos, seltype, done);

	if (done) {
		// button was released, only now set the actual X selection
		auto selection = sel.selection();
		m_x11.selection().setSelection(selection, ev.time());
	}
}

void XEventHandler::expose() {
	m_nst.term().redraw();
}

void XEventHandler::visibilityChange(const xpp::VisibilityEvent &ev) {
	m_x11.setVisible(ev.state() != xpp::VisibilityState::FULLY_OBSCURED);
}

void XEventHandler::unmap() {
	m_x11.setVisible(false);
}

void XEventHandler::focus(const xpp::FocusChangeEvent &ev) {
	if (ev.mode() == xpp::NotifyMode::GRAB)
		return;

	m_x11.focusChange(ev.haveFocus());
}

std::optional<std::string_view>
XEventHandler::customKeyMapping(const xpp::KeySymID keysym, const xpp::InputMask state) const {
	// Check for mapped keys out of X11 function keys.
	if (const bool found = config::MAPPED_KEYS.count(keysym); found == 0) {
		// if the key is not explicitly mapped and it is outside the range of
		// X11 function keys, don't continue
		if ((xpp::raw_key(keysym) & 0xFFFF) < 0xFD00) {
			return {};
		}
	}

	const auto tmode = m_twin.mode();

	for (auto [it, end] = config::KEYS.equal_range(Key{keysym}); it != end; it++) {
		auto &key = *it;

		if (!state_matches(key.mask, state))
			continue;
		else if (!key.matchesAppKeypad(tmode))
			continue;
		else if (!key.matchesAppCursor(tmode))
			continue;

		return key.seq;
	}

	return {};
}

void XEventHandler::keyPress(const xpp::KeyEvent &ev) {
	const auto tmode = m_twin.mode();

	if (tmode[WinMode::KBDLOCK])
		return;

	const auto ksym = m_x11.m_input.lookupString(ev, m_key_buf);

	// 1. shortcuts
	for (auto &sc: m_kbd_shortcuts) {
		if (ksym == sc.keysym && state_matches(sc.mod, ev.state())) {
			sc.func();
			return;
		}
	}

	// 2. custom keys from nst_config.hxx
	if (auto seq = customKeyMapping(ksym, ev.state()); seq) {
		m_nst.tty().write(*seq, TTY::MayEcho{true});
		return;
	}

	// 3. composed string from input method
	if (m_key_buf.empty())
		return;

	if (m_key_buf.size() == 1 && ev.state()[xpp::InputModifier::MOD1]) {
		if (tmode[WinMode::EIGHT_BIT]) {
			if (m_key_buf[0] < 0177) {
				Rune c = m_key_buf[0] | 0x80;
				m_key_buf.clear();
				utf8::encode(c, m_key_buf);
			}
		} else {
			m_key_buf.resize(2);
			m_key_buf[1] = m_key_buf[0];
			m_key_buf[0] = '\033';
		}
	}

	m_nst.tty().write(m_key_buf, TTY::MayEcho{true});
}

void XEventHandler::clientMessage(const xpp::ClientMessageEvent &msg) {
	if (msg.type() == atoms::xembed && msg.format() == 32) {
		// See xembed specs: http://standards.freedesktop.org/xembed-spec/xembed-spec-latest.html
		const XEmbedMessage xembed{XEmbedMessage{msg.data().l[1]}};

		if (xembed == XEmbedMessage::FOCUS_IN) {
			m_x11.embeddedFocusChange(true);
		} else if (xembed == XEmbedMessage::FOCUS_OUT) {
			m_x11.embeddedFocusChange(false);
		}
	} else if (msg.type() == xpp::atoms::icccm_wm_protocols && msg.format() == 32) {
		// we indicated that we support the delete window WM protocol,
		// so react to them - these occur e.g. if you click the window
		// close button rendered by the WM.
		const xpp::AtomID protocol(static_cast<xpp::AtomID>(msg.data().l[0]));

		if (protocol == xpp::atoms::icccm_wm_delete_window) {
			m_nst.tty().hangup();
		}
	}
}

void XEventHandler::resize(const xpp::ConfigureEvent &config) {
	const auto new_size = Extent{config.extent()};

	if (new_size != m_twin.winExtent()) {
		m_x11.setWinSize(new_size);
		m_nst.resizeConsole();
	}
}

void XEventHandler::propertyNotify(const xpp::PropertyEvent &ev) {
	// PropertyNotify is only turned on when there is some
	// INCR transfer happening for the selection retrieval.

	if (ev.state() != xpp::PropertyNotification::NEW_VALUE)
	       return;

	const auto property = ev.property();

	if (property == xpp::atoms::primary_selection ||
			property == xpp::atoms::clipboard) {
		handleSelectionEvent(property);
	}
}

void XEventHandler::selectionNotify(const xpp::SelectionEvent &ev) {
	return handleSelectionEvent(ev.property());
}

void XEventHandler::handleSelectionEvent(const xpp::AtomID selprop) {
	auto &win = m_x11.window();
	auto &term = m_nst.term();
	xpp::XWindow::PropertyInfo info;
	xpp::RawProperty prop{BUFSIZ};
	const bool bracketed_paste = m_twin.checkFlag(WinMode::BRKT_PASTE);

	if (selprop == xpp::AtomID::INVALID)
		return;

	do {
		try {
			win.getRawProperty(selprop, info, prop);
		} catch (const std::exception &ex) {
			std::cerr << "Clipboard property retrieval failed: "
				<< ex.what() << std::endl;
			return;
		}

		if (m_event.isPropertyNotify() && prop.length == 0 && prop.left == 0) {
			/*
			 * If there is some PropertyNotify with no data, then
			 * this is the signal of the selection owner that all
			 * data has been transferred. We won't need to receive
			 * PropertyNotify events anymore.
			 */
			m_x11.changeEventMask(xpp::EventMask::PROPERTY_CHANGE, false);
		}

		if (info.type == atoms::incr) {
			// an incremental selection content transfer started,
			// see https://tronche.com/gui/x/icccm/sec-2.html#s-2.7.2
			/*
			 * Activate the PropertyNotify events so we receive
			 * when the selection owner does send us the next
			 * chunk of data.
			 */
			m_x11.changeEventMask(xpp::EventMask::PROPERTY_CHANGE, true);

			/// Deleting the property is the transfer start signal.
			win.delProperty(selprop);
			continue;
		}

		/*
		 * As seen in Selection::selection():
		 * Line endings are inconsistent in the terminal and GUI world
		 * copy and pasting. When receiving some selection data,
		 * replace all '\n' with '\r'.
		 */
		auto ptr = prop.data.get();
		std::replace(ptr, ptr + prop.length, '\n', '\r');

		if (bracketed_paste && prop.offset == 0)
			term.reportPaste(true);

		m_nst.tty().write(prop.view(), TTY::MayEcho{true});

		if (bracketed_paste && prop.left == 0)
			term.reportPaste(false);
		/* number of 32-bit chunks returned */
		prop.offset += prop.length;
	} while (prop.left > 0);

	// Deleting the property again tells the selection owner to send the
	// next data chunk in the property.
	win.delProperty(selprop);
}

void XEventHandler::selectionClear() {
	if (config::SEL_CLEAR) {
		m_nst.selection().clear();
	}
}

void XEventHandler::selectionRequest(const xpp::SelectionRequestEvent &req) {
	xpp::Event raw_response{xpp::EventType::SELECTION_NOTIFY};
	xpp::SelectionEvent response{raw_response};
	response.setRequestor(req.requestor());
	response.setSelection(req.selection());
	response.setTarget(req.target());
	response.setTime(req.time());
	// reject by default, if nothing matches below
	response.setProperty(xpp::AtomID::INVALID);

	xpp::XWindow requestor{req.requestor()};
	const xpp::AtomID target{req.target()};
	const xpp::AtomID req_prop = req.property() == xpp::AtomID::INVALID ?
			target : req.property();

	auto &xsel = m_x11.selection();

	if (target == atoms::targets) {
		// respond with the supported type.
		xpp::Property<xpp::AtomID> tgt_format{xsel.targetFormat()};

		requestor.setProperty(req_prop, tgt_format);
		response.setProperty(req_prop);
	} else if (target == xsel.targetFormat() || target == xpp::atoms::string_type) {
		// with XA_STRING (string_type) non ascii characters may be
		// incorrect in the requestor. It is not our problem, use utf8.
		try {
			auto seltext = xsel.getSelection(req.selection());
			if (!seltext.empty()) {

				if (target == xpp::atoms::string_type) {
					xpp::Property<const char *> sel_ascii{seltext.c_str()};
					requestor.setProperty(req_prop, sel_ascii);
				} else {
					xpp::Property<xpp::utf8_string> sel_utf8{xpp::utf8_string(seltext)};
					requestor.setProperty(req_prop, sel_utf8);
				}
			}
		} catch (const std::exception &ex) {
			std::cerr << "Failed to handle clipboard selection " << req.selection() << ": " << ex.what() << std::endl;
			return;
		}

		response.setProperty(req_prop);
	}

	try {
		// all done, send a notification to the listener.
		requestor.sendEvent(raw_response);
	} catch(const std::exception &ex) {
		std::cerr << "Error sending SelectionNotify event: " << ex.what() << std::endl;
	}
}

void XEventHandler::buttonPress(const xpp::ButtonEvent &ev) {
	const auto button = ev.buttonNr();
	const auto force_mouse = ev.state().anyOf(config::FORCE_MOUSE_MOD);

	m_buttons.setPressed(button);

	if (m_twin.inMouseMode() && !force_mouse) {
		handleMouseReport(ev);
		return;
	} else if (handleMouseAction(ev)) {
		return;
	} else if (button == xpp::Button::BUTTON1) {
		const auto snap = m_x11.selection().handleClick();
		const auto pos = m_twin.toCharPos(DrawPos{ev.pos()});
		m_nst.selection().start(pos, snap);
	}
}

void XEventHandler::buttonRelease(const xpp::ButtonEvent &ev) {
	const auto button = ev.buttonNr();
	const auto force_mouse = ev.state().anyOf(config::FORCE_MOUSE_MOD);

	m_buttons.setReleased(button);

	if (m_twin.inMouseMode() && !force_mouse) {
		handleMouseReport(ev);
		return;
	} else if (handleMouseAction(ev)) {
		return;
	} else if (button == xpp::Button::BUTTON1) {
		handleMouseSelection(ev, /*done =*/ true);
	}
}

void XEventHandler::pointerMovedEvent(const xpp::PointerMovedEvent &ev) {
	const auto force_mouse = ev.state().anyOf(config::FORCE_MOUSE_MOD);

	if (m_twin.inMouseMode() && !force_mouse) {
		handleMouseReport(ev);
	} else {
		handleMouseSelection(ev);
	}
}

} // end ns
