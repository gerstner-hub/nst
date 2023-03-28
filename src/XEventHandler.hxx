#ifndef NST_XEVENT_HANDLER_HXX
#define NST_XEVENT_HANDLER_HXX

// C++
#include <bitset>
#include <optional>
#include <vector>

// X++
#include "X++/Event.hxx"
#include "X++/helpers.hxx"
#include "X++/types.hxx"
#include "X++/fwd.hxx"

// nst
#include "types.hxx"

namespace nst {

class Nst;
class XSelection;
class X11;

/// Represents the current mouse button press state.
class PressedButtons :
		public std::bitset<11> {
public: // data

	static constexpr xpp::Button NO_BUTTON{12};

public:

	/// returns the position of the lowest button pressed, or NO_BUTTON
	xpp::Button firstButton() const {
		for (size_t bit = 0; bit < size(); bit++) {
			if (this->test(bit))
				return xpp::Button{static_cast<unsigned int>(bit) + 1};
		}

		return NO_BUTTON;
	}

	bool valid(const xpp::Button button) const {
		return button >= xpp::Button::BUTTON1 && button < NO_BUTTON;
	}

	void setPressed(const xpp::Button button) {
		if (valid(button)) {
			this->set(index(button), true);
		}
	}

	void setReleased(const xpp::Button button) {
		if (valid(button)) {
			this->set(index(button), false);
		}
	}

	static bool isScrollWheel(const xpp::Button button) {
		return button == xpp::Button::BUTTON4 || button == xpp::Button::BUTTON5;
	}
protected:

	size_t index(const xpp::Button button) const {
		return xpp::raw_button(button) - 1;
	}
};

/// Implementation of XEvent callback handlers
/**
 * This class reacts to X11 events related to the input system, the windowing
 * system etc. As a result of the events a close interaction with the X11 type
 * and other components of Nst is necessary.
 **/
class XEventHandler {
public: // functions

	explicit XEventHandler(Nst &nst);

	/// Checks for and processes X11 events
	/**
	 * This returns \c true if any type of event occured, otherwise \c
	 * false.
	 **/
	bool checkEvents();

protected: // functions

	/// processes the currently set X11 event
	void process();

	void expose();
	void visibilityChange(const xpp::VisibilityEvent&);
	void unmap();
	void keyPress(const XKeyEvent &);
	void clientMessage(const xpp::ClientMessageEvent &);
	void resize(const xpp::ConfigureEvent &);
	void focus(const xpp::FocusChangeEvent&);
	void buttonRelease(const xpp::ButtonEvent &);
	void buttonPress(const xpp::ButtonEvent&);
	void pointerMovedEvent(const xpp::PointerMovedEvent &);
	void propertyNotify(const xpp::PropertyEvent &);
	void selectionNotify(const xpp::SelectionEvent &);
	void selectionClear();
	void selectionRequest(const xpp::SelectionRequestEvent &);

	/// Handles a selection input event provided in the property \c selprop
	void handleSelectionEvent(const xpp::AtomID selprop);
	template <typename EVENT>
	void handleMouseSelection(const EVENT &, const bool done = false);
	template <typename EVENT>
	void handleMouseReport(const EVENT &);
	bool checkMouseReport(const xpp::ButtonEvent &ev, xpp::Button &button, int &code);
	bool checkMouseReport(const xpp::PointerMovedEvent &ev, xpp::Button &button, int &code);
	bool handleMouseAction(const xpp::ButtonEvent &ev);

	/// Returns an output sequence mapped to the given input event.
	/**
	 * \return
	 * 	The string sequence associated with the key input event or
	 * 	nullopt_t if nothing is mapped.
	 **/
	std::optional<std::string_view> customKeyMapping(KeySym k, unsigned state) const;

protected: // data

	Nst &m_nst;
	X11 &m_x11;
	XSelection &m_xsel;
	const std::vector<MouseShortcut> m_mouse_shortcuts;
	const std::vector<KbdShortcut> m_kbd_shortcuts;
	PressedButtons m_buttons; /// Bit field of pressed buttons.
	CharPos m_old_mouse_pos;
	xpp::Event m_event; /// The currently handled event
};

} // end ns

#endif // inc. guard
