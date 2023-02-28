// X++
#include "X++/atoms.hxx"

// nst
#include "nst.hxx"
#include "XSelection.hxx"
#include "x11.hxx"

namespace nst {

XSelection::XSelection(Nst &nst) :
		m_nst{nst}, m_x11{nst.x11()}
{}

void XSelection::init() {
	m_last_click.mark();
	m_penultimate_click.mark();
	m_primary.clear();
	m_clipboard.clear();
	try {
		m_target_fmt = xpp::atoms::ewmh_utf8_string;
	} catch (const xpp::XDisplay::AtomMappingError &) {
		m_target_fmt = xpp::atoms::string_type;
	}
}

void XSelection::setSelection(const std::string_view str, Time t) {
	if (str.empty())
		return;

	m_primary = str;

	const auto &display = m_x11.display();
	const auto &primary = xpp::atoms::primary_selection;
	auto &our_window = m_x11.window();

	our_window.makeSelectionOwner(primary, t);
	if (auto owner = display.selectionOwner(primary); !owner || *owner != our_window)
		// we could not become the new selection owner
		m_nst.selection().clear();
}

void XSelection::copyPrimaryToClipboard() {
	m_clipboard = m_primary;

	if (m_primary.empty())
		return;

	const auto &clipboard = xpp::atoms::clipboard;
	m_x11.window().makeSelectionOwner(clipboard);
}

const std::string& XSelection::getSelection(const xpp::AtomID which) const {
	if (which == xpp::atoms::primary_selection) {
		return m_primary;
	} else if (which == xpp::atoms::clipboard) {
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

	if (m_penultimate_click.elapsed() <= config::TRIPLE_CLICK_TIMEOUT) {
		ret = Selection::Snap::LINE;
	} else if (m_last_click.elapsed() <= config::DOUBLE_CLICK_TIMEOUT) {
		ret = Selection::Snap::WORD;
	}

	m_penultimate_click = m_last_click;
	m_last_click.mark();

	return ret;
}

} // end ns
