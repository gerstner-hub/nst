#ifndef NST_XEVENT_HANDLER_HXX
#define NST_XEVENT_HANDLER_HXX

// C++
#include <optional>
#include <tuple>
#include <vector>

// X++
#include "X++/Event.hxx"
#include "X++/helpers.hxx"
#include "X++/types.hxx"
#include "X++/fwd.hxx"

// nst
#include "types.hxx"
#include "PressedButtons.hxx"

namespace nst {

class Nst;
class X11;
struct TermWindow;

/// Implementation of XEvent callback handlers
/**
 * This class reacts to X11 events related to the input system, the windowing
 * system etc. As a result of this a close interaction with the X11 type
 * is necessary.
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
	void keyPress(const xpp::KeyEvent &);
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

	/// Handles a selection input event provided in the property \c selprop.
	void handleSelectionEvent(const xpp::AtomID selprop);

	template <typename EVENT>
	void handleMouseSelection(const EVENT &, const bool done = false);

	/// Handles mouse event reporting on TTY level for both PointerMovedEvent and ButtonEvent.
	template <typename EVENT>
	void handleMouseReport(const EVENT &);

	/// Checks whether the given event should be reported on TTY level via escape codes.
	/**
	 * If a value is returned then mouse reporting should be performed.
	 * m_old_mouse_pos will be set to the new terminal mouse position
	 * reported by the event.
	 *
	 * The returned tuple contains the event button number to report
	 * and the base escape code to be used for the reporting.
	 **/
	std::optional<std::tuple<xpp::Button, int>> checkMouseReport(const xpp::ButtonEvent &ev);
	/// \c see checkMouseReport(const xpp::ButtonEvent&)
	std::optional<std::tuple<xpp::Button, int>> checkMouseReport(const xpp::PointerMovedEvent &ev);

	/// Check mouse shortcuts and execute a possibly configured action for the given event.
	/**
	 * \return Whether a mouse action was found and executed.
	 **/
	bool handleMouseAction(const xpp::ButtonEvent &ev);

	/// Returns an output sequence mapped to the given input event.
	/**
	 * \return
	 * 	The string sequence associated with the key input event or
	 * 	nullopt_t if nothing is mapped.
	 **/
	std::optional<std::string_view> customKeyMapping(const xpp::KeySymID keysym, const xpp::InputMask state) const;

protected: // data

	Nst &m_nst;
	X11 &m_x11;
	const TermWindow &m_twin;
	const std::vector<MouseShortcut> m_mouse_shortcuts;
	const std::vector<KbdShortcut> m_kbd_shortcuts;
	PressedButtons m_buttons; /// Bit field of pressed buttons.
	CharPos m_old_mouse_pos; /// The last seen mouse position in terminal coordinates
	xpp::Event m_event; /// The currently handled event.
	std::string m_key_buf; /// reused input sequence string for XInput
};

} // end ns

#endif // inc. guard
