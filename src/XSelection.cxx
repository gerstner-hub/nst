// X++
#include "X++/atoms.hxx"
#include "X++/XDisplay.hxx"

// nst
#include "nst.hxx"
#include "XSelection.hxx"
#include "WindowSystem.hxx"

namespace nst {

XSelection::XSelection(Nst &nst) :
		m_nst{nst}, m_wsys{nst.wsys()}
{}

void XSelection::init() {
	for (auto button: {xpp::Button::BUTTON1, xpp::Button::BUTTON3}) {
		auto &state = m_click_state[button];
		state.last_click.mark();
		state.penultimate_click.mark();
	}
	m_primary.clear();
	m_clipboard.clear();
	try {
		m_target_fmt = xpp::atoms::ewmh_utf8_string;
	} catch (const xpp::XDisplay::AtomMappingError &) {
		m_target_fmt = xpp::atoms::string_type;
	}
}

void XSelection::setSelection(const std::string_view str, const Time t) {
	if (str.empty())
		return;

	m_primary = str;

	const auto primary = xpp::atoms::primary_selection;
	auto &our_window = m_wsys.window();

	our_window.makeSelectionOwner(primary, t);
	if (auto owner = xpp::display.selectionOwner(primary); !owner || *owner != our_window)
		// we could not become the new selection owner
		m_nst.selection().clear();
}

void XSelection::copyPrimaryToClipboard() {
	m_clipboard = m_primary;

	if (m_primary.empty())
		return;

	const auto clipboard = xpp::atoms::clipboard;
	m_wsys.window().makeSelectionOwner(clipboard);
}

const std::string& XSelection::getSelection(const xpp::AtomID which) const {
	if (which == xpp::atoms::primary_selection) {
		return m_primary;
	} else if (which == xpp::atoms::clipboard) {
		return m_clipboard;
	}

	throw std::runtime_error("invalid selection requested");
}

Selection::Snap XSelection::handleClick(const xpp::Button button) {
	// If the user left-clicks below predefined timeouts specific snapping
	// behaviour is exposed.
	auto ret = Selection::Snap::NONE;

	auto it = m_click_state.find(button);
	if (it == m_click_state.end())
		return ret;

	auto &state = it->second;

	if (state.penultimate_click.elapsed() <= config::TRIPLE_CLICK_TIMEOUT) {
		ret = Selection::Snap::LINE;
	} else if (state.last_click.elapsed() <= config::DOUBLE_CLICK_TIMEOUT) {
		ret = Selection::Snap::WORD;
	}

	state.newClick();

	return ret;
}

} // end ns
