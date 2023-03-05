#ifndef NST_INPUT_HXX
#define NST_INPUT_HXX

// C++
#include <memory>

// X11
#include <X11/Xlib.h>

// X++
#include "X++/XWindow.hxx"
#include "types.hxx"

namespace nst {

/// X11 Input Method Handling.
struct Input {
public: // functions

	explicit Input(xpp::XWindow &win) :
		m_win{win}
	{}
	Input(const Input &) = delete;
	Input& operator=(const Input&) = delete;
	~Input();
	void close();
	bool open();
	/// Tries to open an input method or installs a callback handler otherwise.
	void tryOpen() {
		if (!open()) {
			installCallback();
		}
	}

	void setSpot(const DrawPos dp);
	void setFocus();
	void unsetFocus();
	/// Looks up a KeySym and string representation of the given event.
	KeySym lookupString(const XKeyEvent &ev, std::string &s);

protected: // functions

	void destroyMethod();
	int destroyContext();
	void removeCallback();
	void installCallback();
	bool haveContext() const { return m_ctx != nullptr; }
	XPointer clientData() { return reinterpret_cast<XPointer>(this); }

	static void instMethodCB(Display *, XPointer inputp, XPointer);
	static void destroyMethodCB(XIM, XPointer inputp, XPointer);
	static int destroyContextCB(XIC, XPointer inputp, XPointer);

protected: // data

	xpp::XWindow &m_win;
	XIM m_method = nullptr;
	XIC m_ctx = nullptr;
	XPoint m_spot = {0, 0};
	std::shared_ptr<void> m_spotlist;
};

} // end ns

#endif // inc. guard
