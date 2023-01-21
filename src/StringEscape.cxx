// nst
#include "nst_config.hxx"
#include "StringEscape.hxx"
#include "TTY.hxx"
#include "nst.hxx"

// stdlib
#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <iostream>

// cosmos
#include "cosmos/formatting.hxx"

namespace nst {

constexpr size_t DEF_BUF_SIZE = 128 * utf8::UTF_SIZE;
/// maximum number of string escape sequence arguments we support
constexpr size_t MAX_STR_ARGS = 16;

StringEscape::StringEscape(Nst &nst) :
	m_nst(nst)
{}

void StringEscape::oscColorResponse(int index, int code) {
	unsigned char r, g, b;

	if (!m_nst.getX11().getColor(index, &r, &g, &b)) {
		std::cerr << "erresc: failed to fetch osc color " << index << "\n";
		return;
	}

	std::string res;

	if (code == 4) {
		// for code 4 type responses also the index is reported back
		res = cosmos::sprintf("\033]4;%d;rgb:%02x%02x/%02x%02x/%02x%02x\007", index, r, r, g, g, b, b);
	} else {
		res = cosmos::sprintf("\033]%d;rgb:%02x%02x/%02x%02x/%02x%02x\007", code, r, r, g, g, b, b);
	}

	m_nst.getTTY().write(res, TTY::MayEcho(true));
}

void StringEscape::setTitle(const char *s) {
	auto &x11 = m_nst.getX11();
	if (s && s[0])
		x11.setTitle(s);
	else
		x11.setDefaultTitle();
}

void StringEscape::setIconTitle(const char *s) {
	auto &x11 = m_nst.getX11();
	if (s && s[0])
		x11.setIconTitle(s);
	else
		x11.setDefaultIconTitle();
}

void StringEscape::process() {
	parseArgs();

	switch (m_esc_type) {
	case Type::OSC: /* Operating System Command */
		if (!processOSC()) {
			// error in OSC command, dump
			dump("erresc: unknown str escape");
		}
		return;
	case Type::SET_TITLE: /* old title set compatibility */
		setTitle(m_args.empty() ? "" : m_args[0].data());
		return;
	case Type::DCS: /* Device Control String */
	case Type::APC: /* Application Program Command */
	case Type::PM:  /* Privacy Message */
	case Type::NONE: /* should never happend */
		return;
	}
}

bool StringEscape::processOSC() {
	auto &term = m_nst.getTerm();
	auto &x11 = m_nst.getX11();
	const int par = m_args.empty() ? 0 : std::atoi(m_args[0].data());
	const auto numargs = m_args.size();

	// handles different color settings and reporting
	auto handle_color = [&](const char *label, const int code, const int colindex) {
		if (numargs < 2)
			return false;

		const auto &arg = m_args[1];

		if (arg == "?")
			// report current color setting
			oscColorResponse(colindex, code);
		else if (!x11.setColorName(colindex, arg.data()))
			std::cerr << "erresc: invalid " << label << " color: " << arg << "\n";
		else
			term.redraw();

		return true;
	};

	// for reference see: https://www.xfree86.org/current/ctlseqs.html
	switch (par) {
		case 0: // change icon name and window title
			if (numargs > 1) {
				const auto &title = m_args[1].data();
				setTitle(title);
				setIconTitle(title);
			}
			break;
		case 1: // change icon name
			if (numargs > 1)
				setIconTitle(m_args[1].data());
			break;
		case 2: // change window title
			if (numargs > 1)
				setTitle(m_args[1].data());
			break;
		case 52: // manipulate selection data
			if (numargs > 2 && config::ALLOWWINDOWOPS) {
				auto decoded = base64::decode(m_args[2]);
				if (!decoded.empty()) {
					x11.getXSelection().setSelection(decoded);
					x11.copyToClipboard();
				} else {
					std::cerr << "erresc: invalid base64\n";
				}
			}
			break;
		case 10: // change text FG color
			return handle_color("foreground", par, config::DEFAULTFG);
		case 11: // change text BG color
			return handle_color("background", par, config::DEFAULTBG);
		case 12: // change text cursor color
			return handle_color("cursor", par, config::DEFAULTCS);
		case 4: /* change color number to RGB value */
			if (numargs < 3)
				return false;
			/* FALLTHROUGH */
		case 104: /* color reset */ {
			const auto name = (par == 4 ? m_args[2] : std::string_view(""));
			const int colindex = (numargs > 1) ? atoi(m_args[1].data()) : -1;

			if (name == "?")
				oscColorResponse(colindex, 4);
			else if (!x11.setColorName(colindex, name.data())) {
				if (par == 104 && numargs <= 1)
					break; /* color reset without parameter */
				std::cerr << "erresc: invalid color index=" << colindex << ", name=" << (name.empty() ? "(null)" : name) << "\n";
			} else {
				// TODO if defaultbg color is changed, borders are dirty
				term.redraw();
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

		// NOTE: c++20 has a better constructor using iterators
		std::string_view sv(&(*it), std::distance(it, end));
		m_args.push_back(sv);

		if (end != m_str.end()) {
			// make sure the views we add to m_args are properly terminated
			*end = '\0';
			// advance to next arg
			it++;
		}

		it = end;
	}

	if (it != m_str.end()) {
		std::cerr << __FUNCTION__ << ": maximum number of arguments exceeded\n";
	}
}

void StringEscape::dump(const std::string_view &prefix) const {
	std::cerr << prefix << " ESC" << static_cast<char>(m_esc_type);

	for (const auto c: m_str) {
		if (c == '\0') {
			std::cerr << '\n';
			return;
		} else if (std::isprint(c)) {
			std::cerr << (char)c;
		} else if (c == '\n') {
			std::cerr << "(\\n)";
		} else if (c == '\r') {
			std::cerr << "(\\r)";
		} else if (c == 0x1b) {
			std::cerr << "(\\e)";
		} else {
			std::cerr << "(" << cosmos::hexnum(static_cast<unsigned>(c), 2).showBase(false) << ")";
		}
	}
	std::cerr << "ESC\\\n";
}

void StringEscape::reset(const Type &type) {
	m_str.clear();
	m_str.reserve(DEF_BUF_SIZE);
	m_args.clear();
	m_esc_type = type;
}

void StringEscape::add(const std::string_view &s) {
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
		 * m_nst.getTerm().m_esc_state.reset();
		 * process();
		 */
		if (m_str.size() > (SIZE_MAX - utf8::UTF_SIZE) / 2)
			return;
		m_str.reserve(m_str.capacity() << 1);
	}

	m_str.append(s);
}

} // end ns
