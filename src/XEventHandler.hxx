#ifndef NST_XEVENT_HANDLER_HXX
#define NST_XEVENT_HANDLER_HXX

// C++
#include <optional>
#include <vector>

// X++
#include "X++/Event.hxx"
#include "X++/XAtom.hxx"

// nst
#include "types.hxx"

namespace nst {

class Nst;
class XSelection;
class X11;

/// Implementation of XEvent callback handlers
/**
 * This class reacts to X11 events related to the input system, the windowing
 * system etc. As a result of the events a close interaction with the X11 type
 * and other components of Nst is necessary.
 **/
class XEventHandler {
public: // functions

	explicit XEventHandler(Nst &nst);

	/// basic runtime initialization of the event handler
	void init();

	/// processes the given single X11 event
	void process(xpp::Event &ev);

protected: // functions

	void expose();
	void visibilityChange(const XVisibilityEvent&);
	void unmap();
	void keyPress(const XKeyEvent &);
	void clientMessage(const XClientMessageEvent &);
	void resize(const XConfigureEvent &);
	void focus(const xpp::Event &);
	void buttonRelease(const XButtonEvent &);
	void buttonPress(const XButtonEvent&);
	void motionEvent(const xpp::Event &);
	void propertyNotify(const xpp::Event &);
	void selectionNotify(const xpp::Event &);
	void selectionClear();
	void selectionRequest(const XSelectionRequestEvent &);

	void handleMouseSelection(const XButtonEvent &, bool done = false);
	void handleMouseReport(const XButtonEvent &);
	bool handleMouseAction(const XButtonEvent &ev, bool is_release);

	///! returns an output sequence mapped to the given input event
	/**
	 * \return
	 * 	The string sequence associated with the key input event or
	 * 	nullopt_t if nothing is mapped.
	 **/
	std::optional<std::string_view> getCustomKeyMapping(KeySym k, unsigned state) const;
	static unsigned getButtonMask(unsigned button);
	static bool match(unsigned mask, unsigned state);

protected: // data

	Nst &m_nst;
	X11 &m_x11;
	XSelection &m_xsel;
	const std::vector<MouseShortcut> m_mouse_shortcuts;
	const std::vector<KbdShortcut> m_kbd_shortcuts;
	xpp::XAtom m_xembed_atom;
	xpp::XAtom m_incr_atom;
	PressedButtons m_buttons; /* bit field of pressed buttons */
	CharPos m_old_mouse_pos;
};

} // end ns

#endif // inc. guard
