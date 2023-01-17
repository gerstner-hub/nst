// nst
#include "nst_config.hxx"
#include "StringEscape.hxx"
#include "TTY.hxx"
#include "nst.hxx"

// stdlib
#include <cstdlib>
#include <cstring>
#include <iostream>

// cosmos
#include "cosmos/formatting.hxx"

namespace nst {

constexpr size_t DEF_BUF_SIZE = 128 * utf8::UTF_SIZE;
constexpr size_t MAX_STR_ARGS = 16;

StringEscape::StringEscape(Nst &nst) :
	m_nst(nst)
{}

void StringEscape::osc4ColorResponse(int num) {
	unsigned char r, g, b;

	if (!m_nst.getX11().getColor(num, &r, &g, &b)) {
		std::cerr << "erresc: failed to fetch osc4 color " << num << "\n";
		return;
	}

	const auto res = cosmos::sprintf("\033]4;%d;rgb:%02x%02x/%02x%02x/%02x%02x\007", num, r, r, g, g, b, b);

	m_nst.getTTY().write(res.c_str(), res.size(), true);
}

void StringEscape::oscColorResponse(int index, int num) {
	unsigned char r, g, b;

	if (!m_nst.getX11().getColor(index, &r, &g, &b)) {
		std::cerr << "erresc: failed to fetch osc color " << index << "\n";
		return;
	}

	const auto res = cosmos::sprintf("\033]%d;rgb:%02x%02x/%02x%02x/%02x%02x\007", num, r, r, g, g, b, b);

	m_nst.getTTY().write(res.c_str(), res.size(), true);
}

void StringEscape::setTitle(const char *s) {
	auto &x11 = m_nst.getX11();
	if (s)
		x11.setTitle(s);
	else
		x11.setDefaultTitle();
}

void StringEscape::setIconTitle(const char *s) {
	auto &x11 = m_nst.getX11();
	if (s)
		x11.setIconTitle(s);
	else
		x11.setDefaultIconTitle();
}

void StringEscape::handle() {
	auto &term = m_nst.getTerm();
	auto &x11 = m_nst.getX11();

	term.resetStringEscape();
	parse();
	const int par = m_args.empty() ? 0 : std::atoi(m_args[0]);
	const char *p = nullptr;

	switch (m_esc_type) {
	case Type::OSC: /* OSC -- Operating System Command */
		// for reference see: https://www.xfree86.org/current/ctlseqs.html
		switch (par) {
		case 0: // change icon name and window title
			if (m_args.size() > 1) {
				setTitle(m_args[1]);
				setIconTitle(m_args[1]);
			}
			return;
		case 1: // change icon name
			if (m_args.size() > 1)
				setIconTitle(m_args[1]);
			return;
		case 2: // change window title
			if (m_args.size() > 1)
				setTitle(m_args[1]);
			return;
		case 52: // manipulate selection data
			if (m_args.size() > 2 && config::ALLOWWINDOWOPS) {
				char *dec = base64::decode(m_args[2]);
				if (dec) {
					x11.getXSelection().setSelection(dec);
					x11.copyToClipboard();
				} else {
					std::cerr << "erresc: invalid base64\n";
				}
			}
			return;
		case 10: // change text FG color
			if (m_args.size() < 2)
				break;

			p = m_args[1];

			if (!std::strcmp(p, "?"))
				oscColorResponse(config::DEFAULTFG, 10);
			else if (!x11.setColorName(config::DEFAULTFG, p))
				std::cerr << "erresc: invalid foreground color: " << p << "\n";
			else
				term.redraw();
			return;
		case 11: // change text BG color
			if (m_args.size() < 2)
				break;

			p = m_args[1];

			if (!std::strcmp(p, "?"))
				oscColorResponse(config::DEFAULTBG, 11);
			else if (!x11.setColorName(config::DEFAULTBG, p))
				std::cerr << "erresc: invalid background color: " << p << "%s\n";
			else
				term.redraw();
			return;
		case 12: // change text cursor color
			if (m_args.size() < 2)
				break;

			p = m_args[1];

			if (!std::strcmp(p, "?"))
				oscColorResponse(config::DEFAULTCS, 12);
			else if (!x11.setColorName(config::DEFAULTCS, p))
				std::cerr << "erresc: invalid cursor color: " << p << "\n";
			else
				term.redraw();
			return;
		case 4: /* change color number to RGB value */
			if (m_args.size() < 3)
				break;
			p = m_args[2];
			/* FALLTHROUGH */
		case 104: /* color reset */ {
			int j = (m_args.size() > 1) ? atoi(m_args[1]) : -1;

			if (p && !std::strcmp(p, "?"))
				osc4ColorResponse(j);
			else if (!x11.setColorName(j, p)) {
				if (par == 104 && m_args.size() <= 1)
					return; /* color reset without parameter */
				std::cerr << "erresc: invalid color j=" << j << ", p=" << (p ? p : "(null)") << "\n";
			} else {
				/*
				 * TODO if defaultbg color is changed, borders
				 * are dirty
				 */
				term.redraw();
			}
			return;
		}
		}
		break;
	case Type::SET_TITLE: /* old title set compatibility */
		setTitle(m_args[0]);
		return;
	case Type::DCS: /* Device Control String */
	case Type::APC: /* Application Program Command */
	case Type::PM:  /* Privacy Message */
	case Type::NONE: /* should never happend */
		return;
	}

	dump("erresc: unknown str");
}

void StringEscape::parse() {
	char *p = m_str.data();
	m_args.clear();

	if (*p == '\0')
		return;

	int c;
	while (m_args.size() < MAX_STR_ARGS) {
		m_args.push_back(p);
		while ((c = *p) != ';' && c != '\0')
			++p;
		if (c == '\0')
			return;
		*p++ = '\0';
	}
}

void StringEscape::dump(const char *prefix) const {
	std::cerr << prefix << " ESC" << static_cast<char>(m_esc_type);

	for (auto c: m_str) {
		if (c == '\0') {
			std::cerr << '\n';
			return;
		} else if (isprint(c)) {
			std::cerr << (char)c;
		} else if (c == '\n') {
			std::cerr << "(\\n)";
		} else if (c == '\r') {
			std::cerr << "(\\r)";
		} else if (c == 0x1b) {
			std::cerr << "(\\e)";
		} else {
			std::cerr << "(" << cosmos::hexnum(c, 2).showBase(false) << ")";
		}
	}
	std::cerr << "ESC\\\n";
}

void StringEscape::reset(const Type &type) {
	m_str.clear();
	m_str.reserve(DEF_BUF_SIZE);
	m_esc_type = type;
}

void StringEscape::add(const char *ch, size_t len) {
	if (m_str.size() + len >= m_str.capacity()) {
		/*
		 * Here is a bug in terminals. If the user never sends
		 * some code to stop the str or esc command, then st
		 * will stop responding. But this is better than
		 * silently failing with unknown characters. At least
		 * then users will report back.
		 *
		 * In the case users ever get fixed, here is the code:
		 */
		/*
		 * term.m_esc_state.reset();
		 * handle();
		 */
		if (m_str.size() > (SIZE_MAX - utf8::UTF_SIZE) / 2)
			return;
		m_str.reserve(m_str.capacity() << 1);
	}

	m_str.append(ch, len);
}

} // end ns
