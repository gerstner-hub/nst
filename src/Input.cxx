// C++
#include <iostream>

// X++
#include "X++/event/KeyEvent.hxx"
#include "X++/helpers.hxx"
#include "X++/XDisplay.hxx"

// nst
#include "Input.hxx"

namespace nst {

Input::~Input() {
	close();
}

void Input::close() {
	m_spotlist.reset();
	// order is important here, the ctx depends on the method, XCloseIM
	// would also implicitly destroy the IC
	if (m_ctx) {
		XDestroyIC(m_ctx);
		m_ctx = nullptr;
	}
	if (m_method) {
		XCloseIM(m_method);
		m_method = nullptr;
	}
}

bool Input::open() {
	XIMCallback imdestroy = { .client_data = clientData(), .callback = destroyMethodCB };
	XICCallback icdestroy = { .client_data = clientData(), .callback = destroyContextCB };

	close();

	m_method = XOpenIM(xpp::display, nullptr, nullptr, nullptr);
	if (!m_method) {
		return false;
	}

	if (XSetIMValues(m_method, XNDestroyCallback, &imdestroy, nullptr) != nullptr) {
		std::cerr << "XSetIMValues: Could not set XNDestroyCallback.\n";
	}

	if (auto ptr = XVaCreateNestedList(0, XNSpotLocation, &m_spot, nullptr); ptr != nullptr) {
		m_spotlist = xpp::make_shared_xptr(ptr);
	}

	// NOTE: this function takes varargs, passing in C++ objects
	// like m_window even with conversion operator does not work
	m_ctx = XCreateIC(m_method,
			       XNInputStyle,
			       XIMPreeditNothing | XIMStatusNothing,
			       XNClientWindow, m_win.id(),
			       XNDestroyCallback, &icdestroy,
			       nullptr);

	if (!m_ctx) {
		std::cerr << "XCreateIC: Could not create input context.\n";
	}

	return true;
}

void Input::installCallback() {
	/*
	 * NOTE: it's unclear in which context exactly these callbacks are
	 * invoked. The documentation is sparse on this. I believe it doesn't
	 * happend in a multithreaded way, which would be problematic for the
	 * current code without synchronization primitves.
	 *
	 * Instead it looks like XNextEvent(), XFilterEvent() are the drivers
	 * for this, which is good, because no multithreading is involved.
	 */
	XRegisterIMInstantiateCallback(xpp::display, nullptr, nullptr, nullptr,
				       &Input::instMethodCB, clientData());
}

void Input::removeCallback() {
	if (!open())
		return;

	XUnregisterIMInstantiateCallback(xpp::display, nullptr, nullptr, nullptr,
					 &instMethodCB, (XPointer)this);
}

void Input::instMethodCB(Display *, XPointer inputp, XPointer) {
	auto &input = *reinterpret_cast<Input*>(inputp);
	input.removeCallback();
}

void Input::destroyMethodCB(XIM, XPointer inputp, XPointer) {
	auto &input = *reinterpret_cast<Input*>(inputp);
	input.destroyMethod();
}

void Input::destroyMethod() {
	// the method is freed by XLib, we just have to reset the member
	m_method = nullptr;
	m_spotlist.reset();
	installCallback();
}

int Input::destroyContext() {
	// the memory is freed by XLib, we just have to reset the member
	m_ctx = nullptr;
	return 1;
}

int Input::destroyContextCB(XIC, XPointer inputp, XPointer) {
	auto &input = *reinterpret_cast<Input*>(inputp);
	return input.destroyContext();
}

void Input::setSpot(const DrawPos dp) {
	if (!haveContext())
		return;

	//const auto dp = m_wsys.termWin().toDrawPos(chp.nextLine());

	m_spot.x = dp.x;
	m_spot.y = dp.y;

	XSetICValues(m_ctx, XNPreeditAttributes, m_spotlist.get(), nullptr);
}

void Input::setFocus() {
	if (!haveContext())
		return;

	XSetICFocus(m_ctx);
}

void Input::unsetFocus() {
	if (!haveContext())
		return;

	XUnsetICFocus(m_ctx);
}

xpp::KeySymID Input::lookupString(const xpp::KeyEvent &ev, std::string &s) {
	int len;
	KeySym sym;

	auto raw = const_cast<XKeyEvent*>(&ev.raw());

	s.resize(64);

	if (haveContext()) {
		Status status;
		len = XmbLookupString(m_ctx, raw, &s[0], s.size() + 1, &sym, &status);
	} else {
		len = XLookupString(raw, &s[0], s.size() + 1, &sym, nullptr);
	}

	s.resize(len);
	return xpp::KeySymID{sym};
}

} // end ns
