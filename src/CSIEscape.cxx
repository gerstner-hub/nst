// libc
#include <limits.h>

// stdlib
#include <cstring>
#include <iostream>

// libcosmos
#include "cosmos/formatting.hxx"

// nst
#include "CSIEscape.hxx"
#include "Term.hxx"
#include "Selection.hxx"
#include "StringEscape.hxx"
#include "TTY.hxx"
#include "nst.hxx"
#include "nst_config.hxx"

namespace nst {

namespace {
constexpr size_t MAX_ARG_SIZE = 16;
}

CSIEscape::CSIEscape(Nst &nst, StringEscape &str_escape) :
		m_nst(nst), m_str_escape(str_escape) {
	m_args.reserve(MAX_ARG_SIZE);
	m_str.reserve(MAX_STR_SIZE);
}

void CSIEscape::parse(void) {
	m_args.clear();

	auto it = m_str.begin();
	if (*it == '?') {
		m_priv = true;
		it++;
	}

	long int v;
	size_t parsed;

	while (it < m_str.end()) {
		try {
			v = std::stol(&(*it), &parsed);
			it += parsed;
		} catch(const std::invalid_argument &) {
			v = 0;
		} catch(const std::out_of_range &) {
			v = -1;
		}

		m_args.push_back(v);
		if (*it != ';' || m_args.size() == MAX_ARG_SIZE)
			break;
		it++;
	}

	m_mode[0] = *it++;
	m_mode[1] = (it < m_str.end()) ? *it : '\0';
}

void CSIEscape::dump(const char *prefix) const {
	std::cerr << prefix << " ESC[";

	for (auto c: m_str) {
		if (std::isprint(c)) {
			std::cerr << c;
		} else if (c == '\n') {
			std::cerr << "(\\n)";
		} else if (c == '\r') {
			std::cerr << "(\\r)";
		} else if (c == 0x1b) {
			std::cerr << "(\\e)";
		} else {
			std::cerr << "(" << cosmos::hexnum(c, 2) << ")";
		}
	}
	std::cerr << "\n";
}

void CSIEscape::handle() {

	ensureArg(0, 0);
	auto &arg0 = m_args[0];
	auto &term = m_nst.getTerm();
	auto &curpos = term.getCursor().getPos();
	const auto trows = term.getNumRows();
	const auto tcols = term.getNumCols();

	switch (m_mode[0]) {
	default:
		/* cosmos_throw RuntimeError("bad CSIEscape"); */
		break;
	case '@': /* ICH -- Insert <n> blank char */
		setDefault(arg0, 1);
		term.insertBlanksAfterCursor(arg0);
		return;
	case 'A': /* CUU -- Cursor <n> Up */
		setDefault(arg0, 1);
		term.moveCursorTo(curpos.prevLine(arg0));
		return;
	case 'B': /* CUD -- Cursor <n> Down */
	case 'e': /* VPR --Cursor <n> Down */
		setDefault(arg0, 1);
		term.moveCursorTo(curpos.nextLine(arg0));
		return;
	case 'i': /* MC -- Media Copy */
		switch (arg0) {
		case 0:
			term.dump();
			break;
		case 1:
			term.dumpLine(curpos);
			break;
		case 2:
			m_nst.getSelection().dump();
			break;
		case 4:
			term.setPrintMode(false);
			break;
		case 5:
			term.setPrintMode(true);
			break;
		}
		return;
	case 'c': /* DA -- Device Attributes */
		if (arg0 == 0)
			m_nst.getTTY().write(config::VTIDEN, std::strlen(config::VTIDEN), 0);
		return;
	case 'b': /* REP -- if last char is printable print it <n> more times */
		setDefault(arg0, 1);
		term.repeatChar(arg0);
		return;
	case 'C': /* CUF -- Cursor <n> Forward */
	case 'a': /* HPR -- Cursor <n> Forward */
		setDefault(arg0, 1);
		term.moveCursorTo(curpos.nextCol(arg0));
		return;
	case 'D': /* CUB -- Cursor <n> Backward */
		setDefault(arg0, 1);
		term.moveCursorTo(curpos.prevCol(arg0));
		return;
	case 'E': /* CNL -- Cursor <n> Down and first col */
		setDefault(arg0, 1);
		term.moveCursorTo({0, curpos.y + arg0});
		return;
	case 'F': /* CPL -- Cursor <n> Up and first col */
		setDefault(arg0, 1);
		term.moveCursorTo({0, curpos.y - arg0});
		return;
	case 'g': /* TBC -- Tabulation clear */
		switch (arg0) {
		case 0: /* clear current tab stop */
			term.setTabAtCursor(false);
			return;
		case 3: /* clear all the tabs */
			term.clearAllTabs();
			return;
		default:
			break;
		}
		break;
	case 'G': /* CHA -- Move to <col> */
	case '`': /* HPA */
		setDefault(arg0, 1);
		term.moveCursorTo({arg0 - 1, curpos.y});
		return;
	case 'H': /* CUP -- Move to <row> <col> */
	case 'f': /* HVP */
		setDefault(arg0, 1);
		ensureArg(1, 1);
		term.moveCursorAbsTo({m_args[1] - 1, arg0 - 1});
		return;
	case 'I': /* CHT -- Cursor Forward Tabulation <n> tab stops */
		setDefault(arg0, 1);
		term.moveToNextTab(arg0);
		return;
	case 'J': /* ED -- Clear screen */
		switch (arg0) {
		case 0: /* below */
			term.clearRegion({curpos, CharPos{tcols - 1, curpos.y}});
			if (curpos.y < trows - 1) {
				term.clearRegion({CharPos{0, curpos.y + 1}, CharPos{tcols - 1, trows - 1}});
			}
			return;
		case 1: /* above */
			if (curpos.y > 1)
				term.clearRegion({CharPos{0, 0}, CharPos{tcols - 1, curpos.y - 1}});
			term.clearRegion({CharPos{0, curpos.y}, curpos});
			return;
		case 2: /* all */
			term.clearRegion({CharPos{0, 0}, CharPos{tcols - 1, trows - 1}});
			return;
		default:
			break;
		}
		break;
	case 'K': /* EL -- Clear line */
		switch (arg0) {
		case 0: /* right */
			term.clearRegion({curpos, CharPos{tcols - 1, curpos.y}});
			return;
		case 1: /* left */
			term.clearRegion({CharPos{0, curpos.y}, curpos});
			return;
		case 2: /* all */
			term.clearRegion({CharPos{0, curpos.y}, CharPos{tcols - 1, curpos.y}});
			return;
		}
		return;
	case 'S': /* SU -- Scroll <n> line up */
		setDefault(arg0, 1);
		term.scrollUp(arg0);
		return;
	case 'T': /* SD -- Scroll <n> line down */
		setDefault(arg0, 1);
		term.scrollDown(arg0);
		return;
	case 'L': /* IL -- Insert <n> blank lines */
		setDefault(arg0, 1);
		term.insertBlankLinesBelowCursor(arg0);
		return;
	case 'l': /* RM -- Reset Mode */
		term.setMode(m_priv, false, m_args);
		return;
	case 'M': /* DL -- Delete <n> lines */
		setDefault(arg0, 1);
		term.deleteLinesBelowCursor(arg0);
		return;
	case 'X': /* ECH -- Erase <n> char */
		setDefault(arg0, 1);
		term.clearRegion({curpos, curpos.nextCol(arg0 -1)});
		return;
	case 'P': /* DCH -- Delete <n> char */
		setDefault(arg0, 1);
		term.deleteColsAfterCursor(arg0);
		return;
	case 'Z': /* CBT -- Cursor Backward Tabulation <n> tab stops */
		setDefault(arg0, 1);
		term.moveToPrevTab(arg0);
		return;
	case 'd': /* VPA -- Move to <row> */
		setDefault(arg0, 1);
		term.moveCursorAbsTo({curpos.x, arg0 - 1});
		return;
	case 'h': /* SM -- Set terminal mode */
		term.setMode(m_priv, true, m_args);
		return;
	case 'm': /* SGR -- Terminal attribute (color) */
		term.setCursorAttrs(m_args);
		return;
	case 'n': /* DSR – Device Status Report (cursor position) */
		if (arg0 == 6) {
			auto buf = cosmos::sprintf("\033[%i;%iR", curpos.y + 1, curpos.x + 1);
			m_nst.getTTY().write(buf.c_str(), buf.size(), 0);
		}
		return;
	case 'r': /* DECSTBM -- Set Scrolling Region */
		if (m_priv) {
			break;
		} else {
			setDefault(arg0, 1);
			ensureArg(1, trows);
			term.setScrollArea(LineSpan{arg0 - 1, m_args[1] - 1});
			term.moveCursorAbsTo({0, 0});
		}
		return;
	case 's': /* DECSC -- Save cursor position (ANSI.SYS) */
		term.cursorControl(Term::TCursor::Control::SAVE);
		return;
	case 'u': /* DECRC -- Restore cursor position (ANSI.SYS) */
		term.cursorControl(Term::TCursor::Control::LOAD);
		return;
	case ' ':
		switch (m_mode[1]) {
		case 'q': /* DECSCUSR -- Set Cursor Style */
			if (arg0 < 0 || static_cast<unsigned>(arg0) >= static_cast<unsigned>(CursorStyle::END))
				break;
			m_nst.getX11().setCursorStyle(static_cast<CursorStyle>(arg0));
			return;
		default:
			break;
		}
		break;
	}

	dump("erresc: unknown csi");
}

int CSIEscape::eschandle(unsigned char ascii) {
	auto &term = m_nst.getTerm();
	auto &esc = term.getEscapeState();
	using Escape = Term::Escape;
	auto &curpos = term.getCursor().getPos();
	auto &x11 = m_nst.getX11();

	switch (ascii) {
	case '[':
		esc.set(Escape::CSI);
		return 0;
	case '#':
		esc.set(Escape::TEST);
		return 0;
	case '%':
		esc.set(Escape::UTF8);
		return 0;
	case 'P': /* DCS -- Device Control String */
	case '_': /* APC -- Application Program Command */
	case '^': /* PM -- Privacy Message */
	case ']': /* OSC -- Operating System Command */
	case 'k': /* old title set compatibility */ {
		const auto esc_type = static_cast<StringEscape::Type>(ascii);
		term.initStrSequence(esc_type);
		return 0;
	}
	case 'n': /* LS2 -- Locking shift 2 */
	case 'o': /* LS3 -- Locking shift 3 */
		term.setCharset(2 + (ascii - 'n'));
		break;
	case '(': /* GZD4 -- set primary charset G0 */
	case ')': /* G1D4 -- set secondary charset G1 */
	case '*': /* G2D4 -- set tertiary charset G2 */
	case '+': /* G3D4 -- set quaternary charset G3 */
		term.setEscCharset(ascii - '(');
		esc.set(Escape::ALTCHARSET);
		return 0;
	case 'D': /* IND -- Linefeed */
		if (curpos.y == term.getScrollArea().bottom) {
			term.scrollUp(1);
		} else {
			term.moveCursorTo(curpos.nextLine());
		}
		break;
	case 'E': /* NEL -- Next line */
		term.moveToNewline(); /* always go to first col */
		break;
	case 'H': /* HTS -- Horizontal tab stop */
		term.setTabAtCursor(true);
		break;
	case 'M': /* RI -- Reverse index */
		if (curpos.y == term.getScrollArea().top) {
			term.scrollDown(1);
		} else {
			term.moveCursorTo(curpos.prevLine());
		}
		break;
	case 'Z': /* DECID -- Identify Terminal */
		m_nst.getTTY().write(config::VTIDEN, strlen(config::VTIDEN), 0);
		break;
	case 'c': /* RIS -- Reset to initial state */
		term.reset();
		x11.resetState();
		break;
	case '=': /* DECPAM -- Application keypad */
		x11.setMode(WinMode::APPKEYPAD, true);
		break;
	case '>': /* DECPNM -- Normal keypad */
		x11.setMode(WinMode::APPKEYPAD, false);
		break;
	case '7': /* DECSC -- Save Cursor */
		term.cursorControl(Term::TCursor::Control::SAVE);
		break;
	case '8': /* DECRC -- Restore Cursor */
		term.cursorControl(Term::TCursor::Control::LOAD);
		break;
	case '\\': /* ST -- String Terminator */
		if (esc.test(Escape::STR_END))
			m_str_escape.process();
		break;
	default:
		std::cerr << "erresc: unknown sequence ESC " << cosmos::hexnum(ascii, 2)
			<< " '" << (std::isprint(ascii) ? ascii : '.') << "'\n";
		break;
	}
	return 1;
}

} // end ns
