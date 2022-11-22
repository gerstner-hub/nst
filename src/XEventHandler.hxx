#ifndef NST_XEVENT_HANDLER_HXX
#define NST_XEVENT_HANDLER_HXX

// X++
#include "X++/Event.hxx"
#include "xtypes.hxx"

namespace nst {

class Nst;

/// Implementation of XEvent callback handlers
class XEventHandler {
public: // functions

	XEventHandler(Nst &nst) :
		m_nst(nst)
	{}

	void process(xpp::Event &ev) {
		switch(ev.getType()) {
			case KeyPress: return kpress(ev);
			case ClientMessage: return cmessage(ev);
			case ConfigureNotify: return resize(ev);
			case VisibilityNotify: return visibility(ev);
			case UnmapNotify: return unmap();
			case Expose: return expose();
			case FocusIn: return focus(ev);
			case FocusOut: return focus(ev);
			case MotionNotify: return bmotion(ev);
			case ButtonPress: return bpress(ev);
			case ButtonRelease: return brelease(ev);
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
			case SelectionRequest: return selrequest(ev);
		}
	}

protected: // functions

	void expose();
	void visibility(const xpp::Event &);
	void unmap();
	void kpress(const xpp::Event &);
	void cmessage(const xpp::Event &);
	void resize(const xpp::Event &);
	void focus(const xpp::Event &);
	void brelease(const xpp::Event&);
	void bpress(const xpp::Event &);
	void bmotion(const xpp::Event &);
	void propnotify(const xpp::Event &);
	void selnotify(const xpp::Event &);
	void selclear();
	void selrequest(const xpp::Event &);

	int getEventRow(const XButtonEvent &);
	int getEventCol(const XButtonEvent &);
	void handleMouseSelection(const XButtonEvent &, bool done = false);
	void handleMouseReport(const XButtonEvent &);
	bool handleMouseAction(const XButtonEvent &ev, bool is_release);

protected: // data

	PressedButtons m_buttons; /* bit field of pressed buttons */
	Nst &m_nst;
};

} // end ns

#endif // inc. guard
