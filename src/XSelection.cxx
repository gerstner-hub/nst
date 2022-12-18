// nst
#include "nst.hxx"
#include "XSelection.hxx"
#include "x11.hxx"

namespace nst {

XSelection::XSelection(Nst &nst) : m_nst(nst), m_x11(nst.getX11()) {}

void XSelection::copyPrimaryToClipboard() {
	m_clipboard = m_primary;
}

void XSelection::setSelection(const std::string_view &str, Time t) {
	if (str.empty())
		return;

	m_primary = str;
	auto& display = m_x11.getDisplay();

	XSetSelectionOwner(display, XA_PRIMARY, m_x11.getWindow(), t);
	if (XGetSelectionOwner(display, XA_PRIMARY) != m_x11.getWindow())
		m_nst.getSelection().clear();
}

void XSelection::init() {
	m_tclick1.mark();
	m_tclick2.mark();
	m_primary.clear();
	m_clipboard.clear();
	try {
		m_target_fmt = m_x11.getAtom("UTF8_STRING");
	} catch (const xpp::XDisplay::AtomMappingError &) {
		m_target_fmt = XA_STRING;
	}
}

Selection::Snap XSelection::handleClick() {
	/*
	 * If the user left-clicks below predefined timeouts specific snapping
	 * behaviour is exposed.
	 */
	auto ret = Selection::Snap::NONE;

	if (m_tclick2.elapsed() <= config::TRIPLECLICKTIMEOUT) {
		ret = Selection::Snap::LINE;
	} else if (m_tclick1.elapsed() <= config::DOUBLECLICKTIMEOUT) {
		ret = Selection::Snap::WORD;
	}

	m_tclick2 = m_tclick1;
	m_tclick1.mark();

	return ret;
}

const std::string* XSelection::getSelection(Atom which) const {
	auto clipboard = m_x11.getAtom("CLIPBOARD");

	if (which == XA_PRIMARY) {
		return &m_primary;
	} else if (which == clipboard) {
		return &m_clipboard;
	}

	return nullptr;
}

} // end ns
