#ifndef NST_XSELECTION_HXX
#define NST_XSELECTION_HXX

// C++
#include <string>
#include <string_view>

// cosmos
#include "cosmos/time/StopWatch.hxx"

// nst
#include "Selection.hxx"

namespace nst {

struct X11;
class Nst;

/// This type manages the X11 specific parts of selection/clipboard buffer handling
struct XSelection {
public: // functions

	explicit XSelection(Nst &nst);
	/// (re)initialize active selections, format and click timestamps
	void init();
	/// sets new content for the primary selection buffer and make nst the owner the selection
	void setSelection(const std::string_view &str, Time t = CurrentTime);
	/// copy the current primary selection buffer to the clipboard buffer and make nst the owner the clipboard
	void copyPrimaryToClipboard();
	/// get the XAtom describing the format of the selection text
	auto getTargetFormat() const { return m_target_fmt; }
	/// returns the current content of selection type \c which
	const std::string& getSelection(const xpp::XAtom which) const;
	/// detect special click sequences and return resulting selection behaviour
	/**
	 * This function measures the time elapsed between click events to
	 * detect special click sequences that enable specific selection
	 * behaviour.
	 *
	 * \return the kind of selection behaviour that was identified, or
	 * Snap::NONE if no special selection behaviour should be used.
	 **/
	Selection::Snap handleClick();

protected: // data

	Nst &m_nst;
	X11 &m_x11;
	xpp::XAtom m_target_fmt; /// the X11 format used for the selection text
	cosmos::MonotonicStopWatch m_last_click;
	cosmos::MonotonicStopWatch m_penultimate_click;
	std::string m_clipboard; /// current clipboard contents
	std::string m_primary; /// current primary selection contents
};

} // end ns

#endif // inc. guard
