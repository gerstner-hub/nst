// C++
#include <ostream>
#include <type_traits>

using namespace std::literals;

// cosmos
#include "cosmos/formatting.hxx"
#include "cosmos/string.hxx"
#include "cosmos/utils.hxx"

// xpp
#include "xpp/atoms.hxx"
#include "xpp/event/ButtonEvent.hxx"
#include "xpp/event/ClientMessageEvent.hxx"
#include "xpp/event/ConfigureEvent.hxx"
#include "xpp/event/FocusChangeEvent.hxx"
#include "xpp/event/KeyEvent.hxx"
#include "xpp/event/PointerMovedEvent.hxx"
#include "xpp/event/PropertyEvent.hxx"
#include "xpp/event/SelectionEvent.hxx"
#include "xpp/event/SelectionRequestEvent.hxx"
#include "xpp/event/VisibilityEvent.hxx"
#include "xpp/formatting.hxx"
#include "xpp/Property.hxx"

// nst
#include "atoms.hxx"
#include "nst_config.hxx"
#include "nst.hxx"
#include "XEventHandler.hxx"
#include "XSelection.hxx"
#include "WindowSystem.hxx"

namespace nst {

namespace {

	bool state_matches(const xpp::InputMask mask, const xpp::InputMask state) {
		return mask[xpp::InputModifier::ANY] || mask == (state - config::IGNORE_MOD);
	}

	xpp::InputModifier button_mask(const xpp::Button button) {
		using xpp::InputModifier;
		using xpp::Button;
		switch(button) {
			default: return InputModifier::NONE;
			case Button::BUTTON1: return InputModifier::BUTTON1;
			case Button::BUTTON2: return InputModifier::BUTTON2;
			case Button::BUTTON3: return InputModifier::BUTTON3;
			case Button::BUTTON4: return InputModifier::BUTTON4;
			case Button::BUTTON5: return InputModifier::BUTTON5;
		};
	}

	/// Determines whether the given key symbol should be processed for custom key mappings.
	bool is_mapped(const xpp::KeySymID keysym) {
		const bool is_x11_function = cosmos::in_range(
			xpp::raw_key(keysym) & 0xFFFF, 0xFD00, 0xFFFF);

		if (is_x11_function)
			return true;

		// Check for mapped keys out of X11 function keys.
		const bool mapped = config::MAPPED_KEYS.count(keysym) != 0;
		if (mapped)
			return true;

		// if the key is not explicitly mapped and it is outside the
		// range of X11 function keys, don't continue.
		return false;
	}

	template <typename EV>
	Selection::Flags get_selection_flags(const EV &ev) {
		constexpr xpp::InputMask MOD_MASK{
			xpp::InputModifier::SHIFT,
			xpp::InputModifier::CONTROL,
			xpp::InputModifier::MOD1,
			xpp::InputModifier::MOD2,
			xpp::InputModifier::MOD3,
			xpp::InputModifier::MOD4,
			xpp::InputModifier::MOD5,
		};
		using Flag = Selection::Flag;
		Selection::Flags flags;

		if (ev.type() == xpp::EventType::BUTTON_RELEASE)
			flags.set(Flag::FINISHED);

		if (ev.state().limit(MOD_MASK) == config::SEL_ALT_MOD)
			flags.set(Flag::ALT);

		if constexpr (std::is_same_v<EV, xpp::ButtonEvent>) {
			if (ev.buttonNr() == xpp::Button::BUTTON3) {
				flags.set(Flag::BACKWARD);
			}
		}

		return flags;
	}

	template <typename EV>
	Selection::Mode get_selection_mode(const EV &ev, const xpp::InputMask force_mouse_mod) {
		const auto state = (ev.state() - xpp::InputModifier::BUTTON1) - force_mouse_mod;

		for (auto [mode, mask]: config::SEL_MASKS) {
			if (state_matches(mask, state)) {
				return mode;
			}
		}

		return Selection::Mode::CONT_RANGE;
	}

} // end anon ns

XEventHandler::XEventHandler(Nst &nst) :
		m_nst{nst},
		m_wsys{nst.wsys()},
		m_twin{m_wsys.termWin()},
		m_mouse_shortcuts{config::get_mouse_shortcuts(nst)},
		m_kbd_shortcuts{config::get_kbd_shortcuts(nst)},
		m_force_mouse_mod{config::FORCE_MOUSE_MOD},
		m_auto_clear_selection{config::SEL_CLEAR}
{}

void XEventHandler::applyConfig() {
	auto &config = m_nst.configFile();

	if (const auto auto_clear_sel = config.asBool("auto_clear_selection"); auto_clear_sel) {
		m_auto_clear_selection = *auto_clear_sel;
	}

	/* Generically check keybinding overrides.
	 * Each configurable keybinding has a unique label. We check for
	 * matching configuration file keys and apply overrides as necessary. */
	for (auto &shortcut: m_kbd_shortcuts) {
		if (shortcut.label.empty())
			continue;

		const auto key = "keybinding_"s + std::string{shortcut.label};

		if (auto keybind = config.asString(key); keybind != std::nullopt) {
			if (*keybind == "[disable]") {
				shortcut.keysym = xpp::KeySymID::NO_SYMBOL;
			} else {
				applyKeyBindingConfig(shortcut, key, *keybind);
			}
		}
	}

	if (const auto mod_string = config.asString("force_mouse_mod"); mod_string) {
		const auto parts = cosmos::split(*mod_string, " ", cosmos::SplitFlags{cosmos::SplitFlag::STRIP_PARTS});
		m_force_mouse_mod.reset();

		for (auto &part: parts) {
			if (const auto mod = xpp::string_to_input_mod(part); mod) {
				m_force_mouse_mod.set(*mod);
			} else {
				m_nst.logger().warn() << "invalid 'force_mouse_mod' setting '" << *mod_string << "': not a valid X11 input modifier name" << std::endl;
				m_force_mouse_mod = config::FORCE_MOUSE_MOD;
				break;
			}
		}
	}
}

void XEventHandler::applyKeyBindingConfig(KbdShortcut &shortcut, const std::string &key, const std::string &binding) {
	auto &logger = m_nst.logger();
	auto parts = cosmos::split(binding, " ", cosmos::SplitFlags{cosmos::SplitFlag::STRIP_PARTS});
	// the last space separated item is the actual KeySymID string.
	// the rest of the parts make up InputModifier strings.
	const auto keysym_str = parts.back();
	xpp::InputMask modmask;

	parts.pop_back();

	for (const auto &mod_str: parts) {
		if (const auto mod = xpp::string_to_input_mod(mod_str); mod) {
			modmask.set(*mod);
		} else {
			logger.error() << "Bad InputModifier for keybinding '" << key << "': "
				<< mod_str << ". Skipping.\n";
			return;
		}
	}

	if (const auto keysym = xpp::string_to_keysym(keysym_str); keysym) {
		shortcut.keysym = *keysym;
	} else {
		logger.error() << "Bad KeySymID for keybinding '" << key << "': "
			<< keysym_str << ". Skipping.\n";
		return;
	}

	// only apply changes to the modifiers now that we know no errors
	// occured.
	shortcut.mod = modmask;
}

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

	StopScrolling ss{false};

	switch (m_event.type()) {
		default: return;
		case Event::KEY_PRESS:           ss = keyPress         (xpp::KeyEvent{m_event}); break;
		case Event::CLIENT_MESSAGE:    return clientMessage    (xpp::ClientMessageEvent{m_event});
		case Event::CONFIGURE_NOTIFY:  return resize           (xpp::ConfigureEvent{m_event});
		case Event::VISIBILITY_NOTIFY: return visibilityChange (xpp::VisibilityEvent{m_event});
		case Event::UNMAP_NOTIFY:      return unmap();
		case Event::EXPOSE:            return expose();
		case Event::FOCUS_IN:          /* FALLTHROUGH */
		case Event::FOCUS_OUT:         return focus            (xpp::FocusChangeEvent{m_event});
		case Event::MOTION_NOTIFY:     return pointerMovedEvent(xpp::PointerMovedEvent{m_event});
		case Event::BUTTON_PRESS:        ss = buttonPress      (xpp::ButtonEvent{m_event}); break;
		case Event::BUTTON_RELEASE:      ss = buttonRelease    (xpp::ButtonEvent{m_event}); break;
		case Event::SELECTION_NOTIFY:  return selectionNotify  (xpp::SelectionEvent{m_event});
		case Event::PROPERTY_NOTIFY:   return propertyNotify   (xpp::PropertyEvent{m_event});
		case Event::SELECTION_CLEAR:   return selectionClear();
		case Event::SELECTION_REQUEST: return selectionRequest (xpp::SelectionRequestEvent{m_event});
	}

	if (ss) {
		m_nst.term().stopScrolling();
	}
}

void XEventHandler::expose() {
	m_nst.term().redraw();
}

void XEventHandler::visibilityChange(const xpp::VisibilityEvent &ev) {
	m_wsys.setVisible(ev.state() != xpp::VisibilityState::FULLY_OBSCURED);
}

void XEventHandler::unmap() {
	m_wsys.setVisible(false);
}

void XEventHandler::focus(const xpp::FocusChangeEvent &ev) {
	if (ev.mode() == xpp::NotifyMode::GRAB)
		return;

	m_wsys.focusChange(ev.haveFocus());
}

std::optional<std::string_view>
XEventHandler::customKeyMapping(const xpp::KeySymID keysym, const xpp::InputMask state) const {
	if (!is_mapped(keysym))
		return {};

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

StopScrolling XEventHandler::keyPress(const xpp::KeyEvent &ev) {
	const auto tmode = m_twin.mode();
	const auto keepscroll = StopScrolling{false};

	m_wsys.hidePointer();

	if (tmode[WinMode::KBDLOCK])
		return keepscroll;

	const auto ksym = m_wsys.m_input.lookupString(ev, m_key_buf);

	if (ksym == xpp::KeySymID::NO_SYMBOL && m_key_buf.empty())
		// lookup failed
		return keepscroll;

	// 1. shortcuts
	for (auto &shortcut: m_kbd_shortcuts) {
		if (ksym == shortcut.keysym && state_matches(shortcut.mod, ev.state())) {
			shortcut.func();
			return shortcut.stop_scrolling;
		}
	}

	// 2. custom keys from configuration
	if (auto seq = customKeyMapping(ksym, ev.state()); seq) {
		m_nst.tty().write(*seq, TTY::MayEcho{true});
		// this pastes use triggered escape sequences so stop scrolling
		return StopScrolling{true};
	}

	if (m_key_buf.empty())
		return keepscroll;

	// 3. composed string from input method
	if (m_key_buf.size() == 1 && ev.state()[xpp::InputModifier::MOD1]) {
		if (tmode[WinMode::EIGHT_BIT]) {
			// signify MOD1 state by setting the eighth bit
			if ((m_key_buf[0] & 0x80) == 0) {
				Rune c = m_key_buf[0] | 0x80;
				m_key_buf.clear();
				utf8::encode(c, m_key_buf);
			}
		} else {
			// NOTE: unclear why in this case this is turned into
			// an escape sequence.
			m_key_buf.resize(2);
			m_key_buf[1] = m_key_buf[0];
			m_key_buf[0] = '\033';
		}
	}

	m_nst.tty().write(m_key_buf, TTY::MayEcho{true});
	return StopScrolling{true};
}

void XEventHandler::clientMessage(const xpp::ClientMessageEvent &msg) {
	if (msg.type() == atoms::xembed && msg.format() == 32) {
		using XEmbed = xpp::XEmbedMessageType;

		switch (XEmbed{msg.data().l[1]}) {
			default: return;
			case XEmbed::FOCUS_IN:  return m_wsys.embeddedFocusChange(true);
			case XEmbed::FOCUS_OUT: return m_wsys.embeddedFocusChange(false);
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
	const Extent new_size{config.extent()};

	if (new_size != m_twin.winExtent()) {
		m_wsys.setWinSize(new_size);
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
	auto &win = m_wsys.window();
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
			m_nst.logger().error() << "Selection property retrieval failed: " << ex.what() << "\n";
			return;
		}

		if (m_event.isPropertyNotify() && prop.length == 0 && prop.left == 0) {
			/*
			 * If there is some PropertyNotify with no data, then
			 * this is the signal of the selection owner that all
			 * data has been transferred. We won't need to receive
			 * PropertyNotify events anymore.
			 */
			m_wsys.changeEventMask(xpp::EventMask::PROPERTY_CHANGE, false);
		}

		if (info.type == atoms::incr) {
			// an incremental selection content transfer started,
			// see https://tronche.com/gui/x/icccm/sec-2.html#s-2.7.2
			/*
			 * Activate the PropertyNotify events so we receive
			 * when the selection owner does send us the next
			 * chunk of data.
			 */
			m_wsys.changeEventMask(xpp::EventMask::PROPERTY_CHANGE, true);

			/// Deleting the property is the transfer start signal.
			win.delProperty(selprop);
			continue;
		}

		/*
		 * As seen in Selection::data():
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
		// number of 32-bit chunks returned
		prop.offset += prop.length;
	} while (prop.left > 0);

	// Deleting the property again tells the selection owner to send the
	// next data chunk in the property.
	win.delProperty(selprop);
}

void XEventHandler::selectionClear() {
	if (m_auto_clear_selection) {
		m_nst.selection().reset();
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

	auto &xsel = m_wsys.selection();

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
					xpp::Property<const char*> sel_ascii{seltext.c_str()};
					requestor.setProperty(req_prop, sel_ascii);
				} else {
					xpp::Property<xpp::utf8_string> sel_utf8{xpp::utf8_string(seltext)};
					requestor.setProperty(req_prop, sel_utf8);
				}
			}
		} catch (const std::exception &ex) {
			m_nst.logger().error() << "Failed to handle selection request for "
				<< req.selection() << ": " << ex.what() << "\n";
			return;
		}

		response.setProperty(req_prop);
	}

	try {
		// all done, send a notification to the listener.
		requestor.sendEvent(raw_response);
	} catch(const std::exception &ex) {
		m_nst.logger().error() << "Error sending SelectionNotify event: " << ex.what() << "\n";
	}
}

StopScrolling XEventHandler::buttonPress(const xpp::ButtonEvent &ev) {
	const auto force_mouse = ev.state().anyOf(m_force_mouse_mod);
	const auto button = ev.buttonNr();
	m_buttons.setPressed(button);

	if (m_twin.inMouseMode() && !force_mouse) {
		handleMouseReport(ev);
	} else if (auto ss = handleMouseAction(ev); ss) {
		return *ss;
	} else if (cosmos::in_list(button, {xpp::Button::BUTTON1, xpp::Button::BUTTON3})) {
		const auto flags = get_selection_flags(ev);
		auto mode = get_selection_mode(ev, m_force_mouse_mod);
		if (const auto click_mode = m_wsys.selection().handleClick(button, flags); click_mode != std::nullopt) {
			mode = *click_mode;
		}
		const auto pos = m_twin.toCharPos(DrawPos{ev.pos()});
		m_nst.selection().start(pos, mode, flags);
	}

	return StopScrolling{false};
}

StopScrolling XEventHandler::buttonRelease(const xpp::ButtonEvent &ev) {
	const auto button = ev.buttonNr();
	const auto force_mouse = ev.state().anyOf(m_force_mouse_mod);

	m_buttons.setReleased(button);

	if (m_twin.inMouseMode() && !force_mouse) {
		handleMouseReport(ev);
	} else if (auto ss = handleMouseAction(ev); ss) {
		return *ss;
	} else if (button == xpp::Button::BUTTON1 || button == xpp::Button::BUTTON3) {
		handleMouseSelection(ev);
	}

	return StopScrolling{false};
}

void XEventHandler::pointerMovedEvent(const xpp::PointerMovedEvent &ev) {
	const auto force_mouse = ev.state().anyOf(m_force_mouse_mod);

	m_wsys.showPointer();

	if (m_twin.inMouseMode() && !force_mouse) {
		handleMouseReport(ev);
	} else {
		handleMouseSelection(ev);
	}
}

std::optional<StopScrolling> XEventHandler::handleMouseAction(const xpp::ButtonEvent &ev) {
	// ignore Button<N>mask for Button<N> - it's set on release
	const auto state = ev.state() - button_mask(ev.buttonNr());
	const bool is_release = ev.type() == xpp::EventType::BUTTON_RELEASE;
	const auto force_mouse = state - m_force_mouse_mod;

	for (auto &shortcut: m_mouse_shortcuts) {
		if (shortcut.release != is_release || shortcut.button != ev.buttonNr())
			continue;

		// exact or forced match
		if (state_matches(shortcut.mod, state) || state_matches(shortcut.mod, force_mouse)) {
			shortcut.func();
			return shortcut.stop_scrolling;
		}
	}

	return std::nullopt;
}

std::optional<std::tuple<xpp::Button, int>>
XEventHandler::checkMouseReport(const xpp::PointerMovedEvent &ev) {
	const auto pos = m_twin.toCharPos(DrawPos{ev.pos()});

	if (pos == m_old_mouse_pos) {
		// no new terminal position has been reached.
		return {};
	} else if (!m_twin.reportMouseMotion() && !m_twin.reportMouseMany()) {
		// mouse reporting is not enabled.
		return {};
	} else if (m_twin.reportMouseMotion() && m_buttons.none()) {
		// WinMode::MOUSEMOTION: no reporting if no button is pressed
		//
		// NOTE: MOUSEMOTION and MOUSEREPORT are mutually exclusive,
		// this condition is maintained in CSIEscape when one is
		// enabled, the other is disabled.
		return {};
	}

	m_old_mouse_pos = pos;
	return std::make_tuple(m_buttons.firstButton(), 32);
}

std::optional<std::tuple<xpp::Button, int>>
XEventHandler::checkMouseReport(const xpp::ButtonEvent &ev) {
	const auto pos = m_twin.toCharPos(DrawPos{ev.pos()});
	auto button = ev.buttonNr();

	// Only buttons 1 through 11 can be encoded.
	if (!m_buttons.valid(button)) {
		return {};
	} else if (ev.type() == xpp::EventType::BUTTON_RELEASE) {
		if (m_twin.doX10Compatibility()) {
			// MODE_MOUSEX10: no button release reporting.
			return {};
		} else if (m_buttons.isScrollWheel(button)) {
			// Don't send release events for the scroll wheel.
			return {};
		}
	}

	m_old_mouse_pos = pos;
	return std::make_tuple(button, 0);
}

template <typename EVENT>
void XEventHandler::handleMouseReport(const EVENT &ev) {

	auto context = checkMouseReport(ev);

	if (!context) {
		return;
	}

	// the button and escape code to report for the mouse motion
	const auto button = std::get<0>(*context);
	int          code = std::get<1>(*context);

	const bool is_release = ev.type() == xpp::EventType::BUTTON_RELEASE;
	const bool report_sgr = m_twin.reportMouseSGR();
	// checkMouseReport already updated the "old" mouse pos
	const auto pos = m_old_mouse_pos;

	// Encode button into a report code.
	// If no button is pressed for a motion event in WinMode::MOUSEMANY, then encode it as a release.
	if ((!report_sgr && is_release) || button == PressedButtons::NO_BUTTON)
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

	// NOTE: this here breaks a bit the encapsulation, we have the choice
	// to pass X11 data into EscapeHandler or to generate CSI sequences
	// in here.
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
		// position out of range for mouse reporting
		return;
	}

	m_nst.tty().write(report, TTY::MayEcho(false));
}

template <typename EVENT>
void XEventHandler::handleMouseSelection(const EVENT &ev) {
	const auto mode = get_selection_mode(ev, m_force_mouse_mod);
	const auto flags = get_selection_flags(ev);
	const auto pos = m_twin.toCharPos(DrawPos{ev.pos()});

	if (const auto finished = m_nst.selection().update(pos, mode, flags); finished) {
		// button was released, only now set the actual X selection
		applySelection(ev.time());
	}
}

void XEventHandler::applySelection(Time time) {
	auto seldata = m_nst.selection().data();
	m_wsys.selection().setSelection(seldata, time);
}

} // end ns
