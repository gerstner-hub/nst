// nst
#include "nst.hxx"
#include "XSelection.hxx"
#include "x11.hxx"

namespace nst {

XSelection::XSelection(Nst &nst) : m_nst(nst), m_x11(nst.getX11()) {}

void XSelection::init() {
	m_last_click.mark();
	m_penultimate_click.mark();
	m_primary.clear();
	m_clipboard.clear();
	try {
		m_target_fmt = m_x11.getXAtom("UTF8_STRING");
	} catch (const xpp::XDisplay::AtomMappingError &) {
		m_target_fmt = xpp::XAtom(XA_STRING);
	}
}

void XSelection::setSelection(const std::string_view &str, Time t) {
	if (str.empty())
		return;

	m_primary = str;

	const auto &display = m_x11.getDisplay();
	const auto primary = xpp::XAtom(XA_PRIMARY);
	auto &our_window = m_x11.getWindow();

	our_window.makeSelectionOwner(primary, t);
	if (auto owner = display.getSelectionOwner(primary); !owner || *owner != our_window)
		// we could not become the new selection owner
		m_nst.getSelection().clear();
}

void XSelection::copyPrimaryToClipboard() {
	m_clipboard = m_primary;

	if (m_primary.empty())
		return;

	const auto clipboard = m_x11.getXAtom("CLIPBOARD");
	m_x11.getWindow().makeSelectionOwner(clipboard);
}

const std::string& XSelection::getSelection(const xpp::XAtom which) const {
	if (which == XA_PRIMARY) {
		return m_primary;
	} else if (which == m_x11.getAtom("CLIPBOARD")) {
		return m_clipboard;
	}

	throw std::runtime_error("invalid selection requested");
}

Selection::Snap XSelection::handleClick() {
	/*
	 * If the user left-clicks below predefined timeouts specific snapping
	 * behaviour is exposed.
	 */
	auto ret = Selection::Snap::NONE;

	if (m_penultimate_click.elapsed() <= config::TRIPLECLICKTIMEOUT) {
		ret = Selection::Snap::LINE;
	} else if (m_last_click.elapsed() <= config::DOUBLECLICKTIMEOUT) {
		ret = Selection::Snap::WORD;
	}

	m_penultimate_click = m_last_click;
	m_last_click.mark();

	return ret;
}

} // end ns
