#ifndef NST_XEVENT_HANDLER_HXX
#define NST_XEVENT_HANDLER_HXX

// C++
#include <vector>

// X++
#include "X++/Event.hxx"

// nst
#include "types.hxx"

namespace nst {

class Nst;
struct TermWindow;
class XSelection;
class X11;

/// Implementation of XEvent callback handlers
class XEventHandler {
public: // functions

	explicit XEventHandler(Nst &nst, TermWindow &twin, XSelection &xsel);

	void process(xpp::Event &ev) {
		switch(ev.getType()) {
			case KeyPress: return kpress(ev.toKeyEvent());
			case ClientMessage: return cmessage(ev.toClientMessage());
			case ConfigureNotify: return resize(ev.toConfigureNotify());
			case VisibilityNotify: return visibility(ev.toVisibilityNotify());
			case UnmapNotify: return unmap();
			case Expose: return expose();
			case FocusIn: return focus(ev);
			case FocusOut: return focus(ev);
			case MotionNotify: return bmotion(ev);
			case ButtonPress: return bpress(ev.toButtonEvent());
			case ButtonRelease: return brelease(ev.toButtonEvent());
			case SelectionNotify: return selnotify(ev);
			/*
			 * PropertyNotify is only turned on when there is some
			 * INCR transfer happening for the selection retrieval.
			 */
			case PropertyNotify: return propnotify(ev);
			/*
			 * Uncomment if you want the selection to disappear
			 * when you select something different in another window.
			 */
#ifdef SELCLEAR
			case SelectionClear: return selclear();
#endif
			case SelectionRequest: return selrequest(ev.toSelectionRequest());
		}
	}

	void init();

protected: // functions

	void expose();
	void visibility(const XVisibilityEvent&);
	void unmap();
	void kpress(const XKeyEvent &);
	void cmessage(const XClientMessageEvent &);
	void resize(const XConfigureEvent &);
	void focus(const xpp::Event &);
	void brelease(const XButtonEvent &);
	void bpress(const XButtonEvent&);
	void bmotion(const xpp::Event &);
	void propnotify(const xpp::Event &);
	void selnotify(const xpp::Event &);
	void selclear();
	void selrequest(const XSelectionRequestEvent &);

	void handleMouseSelection(const XButtonEvent &, bool done = false);
	void handleMouseReport(const XButtonEvent &);
	bool handleMouseAction(const XButtonEvent &ev, bool is_release);

	const char* getCustomKey(KeySym k, unsigned state) const;
	static unsigned getButtonMask(unsigned button);
	static bool match(uint mask, uint state);

protected: // data

	PressedButtons m_buttons; /* bit field of pressed buttons */
	Nst &m_nst;
	TermWindow &m_twin;
	XSelection &m_xsel;
	X11 &m_x11;
	Atom m_xembed_atom;
	CharPos m_old_mouse_pos;
	const std::vector<MouseShortcut> m_mouse_shortcuts;
	const std::vector<KbdShortcut> m_kbd_shortcuts;
};

} // end ns

#endif // inc. guard
