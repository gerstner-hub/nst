#pragma once

// C++
#include <optional>
#include <tuple>
#include <vector>

// xpp
#include "xpp/Event.hxx"
#include "xpp/helpers.hxx"
#include "xpp/types.hxx"
#include "xpp/fwd.hxx"

// nst
#include "fwd.hxx"
#include "types.hxx"
#include "PressedButtons.hxx"

namespace nst {


/// Implementation of XEvent callback handlers.
/**
 * This class reacts to X11 events related to the input system, the windowing
 * system etc. As a result of this a close interaction with the WindowSystem type
 * is necessary.
 **/
class XEventHandler {
public: // functions

	explicit XEventHandler(Nst &nst);

	void applyConfig();

	/// Checks for and processes X11 events
	/**
	 * This returns `true` if any type of event occured, otherwise `false`.
	 **/
	bool checkEvents();

protected: // functions

	/// Processes the currently set X11 event.
	void process();

	void expose();
	void visibilityChange(const xpp::VisibilityEvent &);
	void unmap();
	StopScrolling keyPress(const xpp::KeyEvent &);
	void clientMessage(const xpp::ClientMessageEvent &);
	void resize(const xpp::ConfigureEvent &);
	void focus(const xpp::FocusChangeEvent &);
	StopScrolling buttonRelease(const xpp::ButtonEvent &);
	StopScrolling buttonPress(const xpp::ButtonEvent &);
	void pointerMovedEvent(const xpp::PointerMovedEvent &);
	void propertyNotify(const xpp::PropertyEvent &);
	void selectionNotify(const xpp::SelectionEvent &);
	void selectionClear();
	/// Another X client requests the selection held by us.
	void selectionRequest(const xpp::SelectionRequestEvent &);

	/// Handles a selection input event provided in the property `selprop`.
	void handleSelectionEvent(const xpp::AtomID selprop);

	/// Handles mouse selection events for both PointerMovedEvent and ButtonEvent.
	template <typename EVENT>
	void handleMouseSelection(const EVENT &);

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
	/// \see checkMouseReport(const xpp::ButtonEvent&)
	std::optional<std::tuple<xpp::Button, int>> checkMouseReport(const xpp::PointerMovedEvent &ev);

	/// Check mouse shortcuts and execute a possibly configured action for the given event.
	/**
	 * \return The StopScrolling behaviour if a mouse action was found and executed, otherwise std::nullopt.
	 **/
	std::optional<StopScrolling> handleMouseAction(const xpp::ButtonEvent &ev);

	/// Returns an output sequence mapped to the given input event.
	/**
	 * \return
	 * 	The string sequence associated with the key input event or
	 * 	nullopt_t if nothing is mapped.
	 **/
	std::optional<std::string_view> customKeyMapping(const xpp::KeySymID keysym, const xpp::InputMask state) const;

	/// Store the currently selected text in the X11 selection buffer.
	void applySelection(Time time);

	/// Applies configuration file settings for the given KbdShortcut.
	void applyKeyBindingConfig(KbdShortcut &shortcut, const std::string &key, const std::string &binding);

protected: // data

	Nst &m_nst;
	WindowSystem &m_wsys;
	const TermWindow &m_twin;
	const std::vector<MouseShortcut> m_mouse_shortcuts;
	std::vector<KbdShortcut> m_kbd_shortcuts;
	PressedButtons m_buttons; ///< Bit field of pressed buttons.
	CharPos m_old_mouse_pos; ///< The last seen mouse position in terminal coordinates
	xpp::Event m_event; ///< The currently handled event.
	std::string m_key_buf; ///< reused input sequence string for XInput
	xpp::InputMask m_force_mouse_mod; ///< runtime configured key modifiers for force mouse mod
	bool m_auto_clear_selection = false; ///< automatically clear selection on ownership loss
};

} // end ns
