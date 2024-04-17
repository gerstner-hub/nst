// nst
#include "nst_config.hxx"
#include "StringEscape.hxx"
#include "TTY.hxx"
#include "nst.hxx"

// C++
#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <ostream>

// cosmos
#include "cosmos/formatting.hxx"
#include "cosmos/string.hxx"

// nst
#include "codecs.hxx"

namespace nst {

namespace {

constexpr size_t DEF_BUF_SIZE = 128 * utf8::UTF_SIZE;
/// maximum number of string escape sequence arguments we support
constexpr size_t MAX_STR_ARGS = 16;

}

StringEscape::StringEscape(Nst &nst) :
		m_nst{nst}
{}

bool StringEscape::isTerminator(const RuneInfo &ri) const {
	switch(ri.asChar()) {
		case '\a':
		case '\030':
		case '\032':
		case '\033':
			return true;
		default:
			return ri.isControlC1();
	}
}

void StringEscape::oscColorResponse(const ColorIndex idx, const int code) {
	uint8_t r, g, b;

	if (!m_nst.wsys().colorManager().toRGB(idx, r, g, b)) {
		m_nst.logger().error() << "erresc: failed to fetch osc color " << cosmos::to_integral(idx) << "\n";
		return;
	}

	std::string res;

	if (code == 4) {
		// for code 4 type responses also the index is reported back
		res = cosmos::sprintf("\033]4;%d;rgb:%02x%02x/%02x%02x/%02x%02x\007", cosmos::to_integral(idx), r, r, g, g, b, b);
	} else {
		res = cosmos::sprintf("\033]%d;rgb:%02x%02x/%02x%02x/%02x%02x\007", code, r, r, g, g, b, b);
	}

	if (res.empty()) {
		// sprintf error occured
		m_nst.logger().error() << "sprintf failed while printing OSC (code " << code << ") response\n";
		return;
	}

	m_nst.tty().write(res, TTY::MayEcho{true});
}

void StringEscape::setTitle(const char *s) {
	auto &wsys = m_nst.wsys();
	if (s && s[0])
		wsys.setTitle(s);
	else
		wsys.setDefaultTitle();
}

void StringEscape::setIconTitle(const char *s) {
	auto &wsys = m_nst.wsys();
	if (s && s[0])
		wsys.setIconTitle(s);
	else
		wsys.setDefaultIconTitle();
}

std::string StringEscape::joinArgs(const size_t start_idx) const {
	std::string ret;

	if (start_idx >= m_args.size())
		return ret;

	for (auto it = m_args.begin() + start_idx; it < m_args.end(); it++) {
		auto &arg = *it;
		if (arg.empty() || !arg[0])
			return ret;

		if (!ret.empty())
			// add the original separator again
			ret += ";";
		ret += arg;
	}

	return ret;
}

void StringEscape::process() {
	parseArgs();

	switch (m_esc_type) {
		case Type::OSC: // Operating System Command
			if (!processOSC()) {
				// error in OSC command, dump
				dump("erresc: unknown str escape");
			}
			return;
		case Type::SET_TITLE: // old title set compatibility
			setTitle(m_args.empty() ? "" : joinArgs(0).c_str());
			return;
		case Type::DCS:  //
		case Type::APC:  // Application Program Command
		case Type::PM:   // Privacy Message
		case Type::NONE: // should never happen
			return;
	}
}

bool StringEscape::processOSC() {
	auto &wsys = m_nst.wsys();
	const int par = m_args.empty() ? 0 : std::atoi(m_args[0].data());
	const auto numargs = m_args.size();

	// handles different color settings and reporting
	auto handle_color = [&](const std::string_view label, const int code, const ColorIndex idx) {
		if (numargs < 2)
			return false;

		const auto arg = m_args[1];

		if (arg == "?")
			// report current color setting
			oscColorResponse(idx, code);
		else if (!wsys.colorManager().setColorName(idx, arg.data()))
			m_nst.logger().error() << "erresc: invalid " << label << " color: " << arg << "\n";
		else
			m_nst.term().setAllDirty();

		return true;
	};

	constexpr int SET_COLOR_INDEX = 4;
	constexpr int RESET_COLOR_INDEX = 104;

	auto &theme = m_nst.theme();

	// for reference see: https://www.xfree86.org/current/ctlseqs.html
	switch (par) {
		case 0: // change icon name _and_ window title
			if (numargs > 1) {
				const auto title = joinArgs(1);
				setTitle(title.c_str());
				setIconTitle(title.c_str());
			}
			break;
		case 1: // change icon name
			if (numargs > 1)
				setIconTitle(joinArgs(1).c_str());
			break;
		case 2: // change window title
			if (numargs > 1)
				setTitle(joinArgs(1).c_str());
			break;
		case 52: // manipulate selection data
			if (numargs > 2 && config::ALLOW_WINDOW_OPS) {
				auto decoded = base64::decode(m_args[2]);
				if (!decoded.empty()) {
					wsys.selection().setSelection(decoded);
					wsys.copyToClipboard();
				} else {
					m_nst.logger().error() << "erresc: invalid base64\n";
				}
			}
			break;
		case 10: // change text FG color
			return handle_color("foreground", par, theme.fg);
		case 11: // change text BG color
			return handle_color("background", par, theme.bg);
		case 12: // change text cursor color
			return handle_color("cursor", par, theme.cursor_color);
		case SET_COLOR_INDEX: // change color number to RGB value
			if (numargs < 3)
				return false;
			/* FALLTHROUGH */
		case RESET_COLOR_INDEX: { // color reset
			const auto name = (par == SET_COLOR_INDEX ? m_args[2] : std::string_view(""));
			const int rawindex = (numargs > 1) ? atoi(m_args[1].data()) : -1;
			const auto colindex = rawindex >= 0 ? ColorIndex(rawindex) : ColorIndex::INVALID;

			if (name == "?")
				oscColorResponse(colindex, SET_COLOR_INDEX);
			else if (!wsys.colorManager().setColorName(colindex, name.data())) {
				if (par == RESET_COLOR_INDEX && numargs <= 1) {
					wsys.resetColors();
					break; // color reset without parameter
				}
				m_nst.logger().error() << "erresc: invalid color index=" << rawindex << ", name=" << (name.empty() ? "(null)" : name) << "\n";
			} else {
				m_nst.term().setAllDirty();
				wsys.clearWindow();
			}
			break;
		}
		default: return false;
	}

	return true;
}


void StringEscape::parseArgs() {
	auto it = m_str.begin();

	// parameters are separated by semilocon, extract them

	while (it != m_str.end() && m_args.size() < MAX_STR_ARGS) {
		auto end = std::find(it, m_str.end(), ';');

		// NOTE: C++20 has a better constructor using iterators
		std::string_view sv(&(*it), std::distance(it, end));
		m_args.push_back(sv);

		if (end != m_str.end()) {
			// make sure the views we add to m_args are properly terminated
			*end = '\0';
			end++;
		}

		it = end;
	}

	if (it != m_str.end()) {
		m_nst.logger().error() << __FUNCTION__ << ": maximum number of arguments exceeded\n";
	}
}

void StringEscape::dump(const std::string_view prefix) const {
	auto &errlog = m_nst.logger().error();
	errlog << prefix << " ESC" << static_cast<char>(m_esc_type);

	for (const auto c: m_str) {
		if (c == '\0') {
			errlog << '\n';
			return;
		} else if (cosmos::printable(c)) {
			errlog << (char)c;
		} else if (c == '\n') {
			errlog << "(\\n)";
		} else if (c == '\r') {
			errlog << "(\\r)";
		} else if (c == 0x1b) {
			errlog << "(\\e)";
		} else {
			errlog << "(" << cosmos::HexNum{static_cast<unsigned>(c), 2}.showBase(false) << ")";
		}
	}
	errlog << "ESC\\\n";
}

void StringEscape::reset(const Type type) {
	m_str.clear();
	m_str.reserve(DEF_BUF_SIZE);
	m_args.clear();
	m_esc_type = type;
}

void StringEscape::add(const std::string_view s) {
	if (m_str.size() + s.size() >= m_str.capacity()) {
		/*
		 * Here is a bug in terminals. If the user never sends
		 * some code to stop the str or esc command, then nst
		 * will stop responding. But this is better than
		 * silently failing with unknown characters. At least
		 * then users will report back.
		 *
		 * In the case users ever get fixed, here is the code:
		 */
		/*
		 * m_nst.term().m_esc_state.reset();
		 * process();
		 */
		if (m_str.size() > (SIZE_MAX - utf8::UTF_SIZE) / 2)
			return;
		m_str.reserve(m_str.capacity() << 1);
	}

	m_str.append(s);
}

} // end ns
