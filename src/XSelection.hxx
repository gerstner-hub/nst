#pragma once

// C++
#include <map>
#include <string>
#include <string_view>

// cosmos
#include "cosmos/time/StopWatch.hxx"

// nst
#include "fwd.hxx"
#include "Selection.hxx"

namespace nst {

/// This type manages the WindowSystem specific parts of selection/clipboard buffer handling.
class XSelection {
public: // functions

	explicit XSelection(Nst &nst);

	/// (re)initialize active selections, format and click timestamps.
	void init();

	/// Sets new content for the primary selection buffer and make nst the owner the selection.
	void setSelection(const std::string_view str, const Time t = CurrentTime);

	/// Returns the current content of selection type \c which.
	const std::string& getSelection(const xpp::AtomID which) const;

	/// Copy the current primary selection buffer to the clipboard buffer and make nst the owner the clipboard.
	void copyPrimaryToClipboard();

	/// Get the AtomID describing the format of the selection text.
	auto targetFormat() const { return m_target_fmt; }

	/// Detect special click sequences and return resulting selection behaviour.
	/**
	 * This function measures the time elapsed between click events to
	 * detect special click sequences that enable specific selection
	 * behaviour.
	 *
	 * \return the kind of selection behaviour that was identified, or
	 * Snap::NONE if no special selection behaviour should be used.
	 **/
	Selection::Snap handleClick(const xpp::Button button);

protected: // types

	struct ClickState {
		cosmos::MonotonicStopWatch last_click;
		cosmos::MonotonicStopWatch penultimate_click;

		void newClick() {
			penultimate_click = last_click;
			last_click.mark();
		}
	};

protected: // data

	Nst &m_nst;
	WindowSystem &m_wsys;
	xpp::AtomID m_target_fmt; /// the X11 format used for the selection text
	std::map<xpp::Button, ClickState> m_click_state;
	std::string m_clipboard; /// current clipboard contents
	std::string m_primary; /// current primary selection contents
};

} // end ns
