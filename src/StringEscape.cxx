// nst
#include "nst_config.h"
#include "StringEscape.hxx"
#include "TTY.hxx"
#include "win.h"

// stdlib
#include <cstdlib>
#include <iostream>

// cosmos
#include "cosmos/formatting.hxx"

nst::STREscape strescseq;

namespace nst {

constexpr size_t DEF_BUF_SIZE = 128 * utf8::UTF_SIZE;
constexpr size_t MAX_STR_ARGS = 16;


static void osc4_color_response(int num) {
	unsigned char r, g, b;

	if (xgetcolor(num, &r, &g, &b)) {
		std::cerr << "erresc: failed to fetch osc4 color " << num << "\n";
		return;
	}

	const auto res = cosmos::sprintf("\033]4;%d;rgb:%02x%02x/%02x%02x/%02x%02x\007", num, r, r, g, g, b, b);

	g_tty.write(res.c_str(), res.size(), true);
}

static void osc_color_response(int index, int num) {
	unsigned char r, g, b;

	if (xgetcolor(index, &r, &g, &b)) {
		std::cerr << "erresc: failed to fetch osc color " << index << "\n";
		return;
	}

	const auto res = cosmos::sprintf("\033]%d;rgb:%02x%02x/%02x%02x/%02x%02x\007", num, r, r, g, g, b, b);

	g_tty.write(res.c_str(), res.size(), true);
}

void STREscape::handle() {
	term.resetStringEscape();
	parse();
	const int par = m_args.empty() ? 0 : std::atoi(m_args[0]);
	const char *p = nullptr;

	switch (m_esc_type) {
	case ']': /* OSC -- Operating System Command */
		switch (par) {
		case 0:
			if (m_args.size() > 1) {
				xsettitle(m_args[1]);
				xseticontitle(m_args[1]);
			}
			return;
		case 1:
			if (m_args.size() > 1)
				xseticontitle(m_args[1]);
			return;
		case 2:
			if (m_args.size() > 1)
				xsettitle(m_args[1]);
			return;
		case 52:
			if (m_args.size() > 2 && config::ALLOWWINDOWOPS) {
				char *dec = base64::decode(m_args[2]);
				if (dec) {
					xsetsel(dec);
					xclipcopy();
				} else {
					std::cerr << "erresc: invalid base64\n";
				}
			}
			return;
		case 10:
			if (m_args.size() < 2)
				break;

			p = m_args[1];

			if (!std::strcmp(p, "?"))
				osc_color_response(config::DEFAULTFG, 10);
			else if (xsetcolorname(config::DEFAULTFG, p))
				std::cerr << "erresc: invalid foreground color: " << p << "\n";
			else
				term.redraw();
			return;
		case 11:
			if (m_args.size() < 2)
				break;

			p = m_args[1];

			if (!std::strcmp(p, "?"))
				osc_color_response(config::DEFAULTBG, 11);
			else if (xsetcolorname(config::DEFAULTBG, p))
				std::cerr << "erresc: invalid background color: " << p << "%s\n";
			else
				term.redraw();
			return;
		case 12:
			if (m_args.size() < 2)
				break;

			p = m_args[1];

			if (!std::strcmp(p, "?"))
				osc_color_response(config::DEFAULTCS, 12);
			else if (xsetcolorname(config::DEFAULTCS, p))
				std::cerr << "erresc: invalid cursor color: " << p << "\n";
			else
				term.redraw();
			return;
		case 4: /* color set */
			if (m_args.size() < 3)
				break;
			p = m_args[2];
			/* FALLTHROUGH */
		case 104: /* color reset */ {
			int j = (m_args.size() > 1) ? atoi(m_args[1]) : -1;

			if (p && !std::strcmp(p, "?"))
				osc4_color_response(j);
			else if (xsetcolorname(j, p)) {
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
	case 'k': /* old title set compatibility */
		xsettitle(m_args[0]);
		return;
	case 'P': /* DCS -- Device Control String */
	case '_': /* APC -- Application Program Command */
	case '^': /* PM -- Privacy Message */
		return;
	}

	dump("erresc: unknown str");
}

void STREscape::parse() {
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

void STREscape::dump(const char *prefix) const {
	std::cerr << prefix << " ESC" << m_esc_type;

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

void STREscape::reset(const char type) {
	m_str.clear();
	m_str.reserve(DEF_BUF_SIZE);
	m_esc_type = type;
}

void STREscape::add(const char *ch, size_t len) {
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
