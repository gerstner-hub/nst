// nst
#include "StringEscape.hxx"
#include "win.h"
#include "nst_config.h"
#include "TTY.hxx"

// stdlib
#include <iostream>

// cosmos
#include "cosmos/formatting.hxx"

nst::STREscape strescseq;

namespace nst {

constexpr size_t STR_BUF_SIZE = 128 * utf8::UTF_SIZE;

static void osc4_color_response(int num) {
	int n;
	char buf[32];
	unsigned char r, g, b;

	if (xgetcolor(num, &r, &g, &b)) {
		std::cerr << "erresc: failed to fetch osc4 color " << num << "\n";
		return;
	}

	n = snprintf(buf, sizeof(buf), "\033]4;%d;rgb:%02x%02x/%02x%02x/%02x%02x\007",
		     num, r, r, g, g, b, b);

	g_tty.write(buf, n, true);
}

static void osc_color_response(int index, int num) {
	int n;
	char buf[32];
	unsigned char r, g, b;

	if (xgetcolor(index, &r, &g, &b)) {
		std::cerr << "erresc: failed to fetch osc color " << index << "\n";
		return;
	}

	n = snprintf(buf, sizeof buf, "\033]%d;rgb:%02x%02x/%02x%02x/%02x%02x\007",
		     num, r, r, g, g, b, b);

	g_tty.write(buf, n, true);
}

void STREscape::handle() {
	term.esc &= ~(ESC_STR_END|ESC_STR);
	parse();
	const int par = narg ? atoi(args[0]) : 0;
	char *p = nullptr;

	switch (type) {
	case ']': /* OSC -- Operating System Command */
		switch (par) {
		case 0:
			if (narg > 1) {
				xsettitle(args[1]);
				xseticontitle(args[1]);
			}
			return;
		case 1:
			if (narg > 1)
				xseticontitle(args[1]);
			return;
		case 2:
			if (narg > 1)
				xsettitle(args[1]);
			return;
		case 52:
			if (narg > 2 && config::ALLOWWINDOWOPS) {
				char *dec = base64::decode(args[2]);
				if (dec) {
					xsetsel(dec);
					xclipcopy();
				} else {
					std::cerr << "erresc: invalid base64\n";
				}
			}
			return;
		case 10:
			if (narg < 2)
				break;

			p = args[1];

			if (!std::strcmp(p, "?"))
				osc_color_response(config::DEFAULTFG, 10);
			else if (xsetcolorname(config::DEFAULTFG, p))
				std::cerr << "erresc: invalid foreground color: " << p << "\n";
			else
				term.redraw();
			return;
		case 11:
			if (narg < 2)
				break;

			p = args[1];

			if (!std::strcmp(p, "?"))
				osc_color_response(config::DEFAULTBG, 11);
			else if (xsetcolorname(config::DEFAULTBG, p))
				std::cerr << "erresc: invalid background color: " << p << "%s\n";
			else
				term.redraw();
			return;
		case 12:
			if (narg < 2)
				break;

			p = args[1];

			if (!std::strcmp(p, "?"))
				osc_color_response(config::DEFAULTCS, 12);
			else if (xsetcolorname(config::DEFAULTCS, p))
				std::cerr << "erresc: invalid cursor color: " << p << "\n";
			else
				term.redraw();
			return;
		case 4: /* color set */
			if (narg < 3)
				break;
			p = args[2];
			/* FALLTHROUGH */
		case 104: /* color reset */ {
			int j = (narg > 1) ? atoi(args[1]) : -1;

			if (p && !std::strcmp(p, "?"))
				osc4_color_response(j);
			else if (xsetcolorname(j, p)) {
				if (par == 104 && narg <= 1)
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
		xsettitle(args[0]);
		return;
	case 'P': /* DCS -- Device Control String */
	case '_': /* APC -- Application Program Command */
	case '^': /* PM -- Privacy Message */
		return;
	}

	dump("erresc: unknown str");
}

void STREscape::parse() {
	int c;
	char *p = buf;

	narg = 0;
	buf[len] = '\0';

	if (*p == '\0')
		return;

	while (narg < STR_ARG_SIZE) {
		args[narg++] = p;
		while ((c = *p) != ';' && c != '\0')
			++p;
		if (c == '\0')
			return;
		*p++ = '\0';
	}
}

void STREscape::dump(const char *prefix) const {
	std::cerr << prefix << " ESC" << type;

	unsigned int c;
	for (size_t i = 0; i < len; i++) {
		c = buf[i] & 0xFF;
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

void STREscape::reset() {
	resize(STR_BUF_SIZE);
}

} // end ns
