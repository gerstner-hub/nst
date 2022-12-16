#ifndef NST_XSELECTION_HXX
#define NST_XSELECTION_HXX

// C++
#include <string_view>
// libX11
#include <X11/Xlib.h>
// cosmos
#include "cosmos/time/Clock.hxx"
// nst
#include "Selection.hxx"

namespace nst {

struct X11;

struct XSelection {
public: // functions

	XSelection(X11 &x11) : m_x11(x11) {}
	void setSelection(const std::string_view &str, Time t = CurrentTime);
	void init();
	void copyPrimaryToClipboard();
	bool havePrimarySelection() const { return !m_primary.empty(); }
	Atom getTargetFormat() const { return m_target_fmt; }
	const std::string* getSelection(Atom which) const;
	Selection::Snap handleClick();

protected: // data

	struct X11 &m_x11;
	Atom m_target_fmt; //! the X11 format used for the selection text
	cosmos::MonotonicStopWatch m_tclick1;
	cosmos::MonotonicStopWatch m_tclick2;
	std::string m_clipboard; //! current clipboard contents
	std::string m_primary; //! current primary selection contents
};

} // end ns

#endif // inc. guard