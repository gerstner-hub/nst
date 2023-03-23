#ifndef NST_XEVENT_HANDLER_HXX
#define NST_XEVENT_HANDLER_HXX

// C++
#include <bitset>
#include <optional>
#include <vector>

// X++
#include "X++/Event.hxx"
#include "X++/event/VisibilityEvent.hxx"
#include "X++/event/FocusChangeEvent.hxx"
#include "X++/types.hxx"

// nst
#include "types.hxx"

namespace nst {

class Nst;
class XSelection;
class X11;

class PressedButtons :
		public std::bitset<11> {
public: // data

	static constexpr size_t NO_BUTTON = 12;

public:

	/// returns the position of the lowest button pressed, or NO_BUTTON
	size_t firstButton() const {
		for (size_t bit = 0; bit < size(); bit++) {
			if (this->test(bit))
				return bit + 1;
		}

		return NO_BUTTON;
	}

	bool valid(const size_t button) const {
		return button >= 1 && button <= size();
	}

	void setPressed(const size_t button) {
		this->set(button - 1, true);
	}

	void setReleased(const size_t button) {
		this->set(button - 1, false);
	}

	static bool isScrollWheel(const size_t button) {
		return button == 4 || button == 5;
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

	/// processes the given single X11 event
	void process(xpp::Event &ev);

protected: // functions

	void expose();
	void visibilityChange(const xpp::VisibilityEvent&);
	void unmap();
	void keyPress(const XKeyEvent &);
	void clientMessage(const XClientMessageEvent &);
	void resize(const XConfigureEvent &);
	void focus(const xpp::FocusChangeEvent&);
	void buttonRelease(const XButtonEvent &);
	void buttonPress(const XButtonEvent&);
	void motionEvent(const xpp::Event &);
	void propertyNotify(const xpp::Event &);
	void selectionNotify(const xpp::Event &);
	void selectionClear();
	void selectionRequest(const XSelectionRequestEvent &);

	void handleMouseSelection(const XButtonEvent &, const bool done = false);
	void handleMouseReport(const XButtonEvent &);
	bool handleMouseAction(const XButtonEvent &ev, bool is_release);

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
};

} // end ns

#endif // inc. guard
