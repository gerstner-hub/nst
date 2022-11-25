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
#include "nst_config.h"
#include "win.h"

namespace nst {

namespace {
constexpr size_t MAX_ARG_SIZE = 16;
}

CSIEscape::CSIEscape(Term &term, Selection &selection, STREscape &strescseq) :
		m_term(term), m_selection(selection), m_strescseq(strescseq) {
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
	auto &cursor = m_term.getCursor();
	const auto trows = m_term.getNumRows();
	const auto tcols = m_term.getNumCols();

	switch (m_mode[0]) {
	default:
		/* cosmos_throw RuntimeError("bad CSIEscape"); */
		break;
	case '@': /* ICH -- Insert <n> blank char */
		setDefault(arg0, 1);
		m_term.insertBlank(arg0);
		return;
	case 'A': /* CUU -- Cursor <n> Up */
		setDefault(arg0, 1);
		m_term.moveCursorTo(cursor.pos.prevLine(arg0));
		return;
	case 'B': /* CUD -- Cursor <n> Down */
	case 'e': /* VPR --Cursor <n> Down */
		setDefault(arg0, 1);
		m_term.moveCursorTo(cursor.pos.nextLine(arg0));
		return;
	case 'i': /* MC -- Media Copy */
		switch (arg0) {
		case 0:
			m_term.dump();
			break;
		case 1:
			m_term.dumpLine(cursor.pos.y);
			break;
		case 2:
			m_selection.dump();
			break;
		case 4:
			m_term.setPrintMode(false);
			break;
		case 5:
			m_term.setPrintMode(true);
			break;
		}
		return;
	case 'c': /* DA -- Device Attributes */
		if (arg0 == 0)
			Nst::getTTY().write(config::VTIDEN, std::strlen(config::VTIDEN), 0);
		return;
	case 'b': /* REP -- if last char is printable print it <n> more times */
		setDefault(arg0, 1);
		if (m_term.getLastChar()) {
			while (arg0-- > 0)
				m_term.putChar(m_term.getLastChar());
		}
		return;
	case 'C': /* CUF -- Cursor <n> Forward */
	case 'a': /* HPR -- Cursor <n> Forward */
		setDefault(arg0, 1);
		m_term.moveCursorTo(cursor.pos.nextCol(arg0));
		return;
	case 'D': /* CUB -- Cursor <n> Backward */
		setDefault(arg0, 1);
		m_term.moveCursorTo(cursor.pos.prevCol(arg0));
		return;
	case 'E': /* CNL -- Cursor <n> Down and first col */
		setDefault(arg0, 1);
		m_term.moveCursorTo({0, cursor.pos.y + arg0});
		return;
	case 'F': /* CPL -- Cursor <n> Up and first col */
		setDefault(arg0, 1);
		m_term.moveCursorTo({0, cursor.pos.y - arg0});
		return;
	case 'g': /* TBC -- Tabulation clear */
		switch (arg0) {
		case 0: /* clear current tab stop */
			m_term.setTabAtCursor(false);
			return;
		case 3: /* clear all the tabs */
			m_term.clearAllTabs();
			return;
		default:
			break;
		}
		break;
	case 'G': /* CHA -- Move to <col> */
	case '`': /* HPA */
		setDefault(arg0, 1);
		m_term.moveCursorTo({arg0 - 1, cursor.pos.y});
		return;
	case 'H': /* CUP -- Move to <row> <col> */
	case 'f': /* HVP */
		setDefault(arg0, 1);
		ensureArg(1, 1);
		m_term.moveCursorAbsTo({m_args[1] - 1, arg0 - 1});
		return;
	case 'I': /* CHT -- Cursor Forward Tabulation <n> tab stops */
		setDefault(arg0, 1);
		m_term.putTab(arg0);
		return;
	case 'J': /* ED -- Clear screen */
		switch (arg0) {
		case 0: /* below */
			m_term.clearRegion({cursor.pos, CharPos{tcols - 1, cursor.pos.y}});
			if (cursor.pos.y < trows - 1) {
				m_term.clearRegion({CharPos{0, cursor.pos.y + 1}, CharPos{tcols - 1, trows - 1}});
			}
			return;
		case 1: /* above */
			if (cursor.pos.y > 1)
				m_term.clearRegion({CharPos{0, 0}, CharPos{tcols - 1, cursor.pos.y - 1}});
			m_term.clearRegion({CharPos{0, cursor.pos.y}, cursor.pos});
			return;
		case 2: /* all */
			m_term.clearRegion({CharPos{0, 0}, CharPos{tcols - 1, trows - 1}});
			return;
		default:
			break;
		}
		break;
	case 'K': /* EL -- Clear line */
		switch (arg0) {
		case 0: /* right */
			m_term.clearRegion({cursor.pos, CharPos{tcols - 1, cursor.pos.y}});
			return;
		case 1: /* left */
			m_term.clearRegion({CharPos{0, cursor.pos.y}, cursor.pos});
			return;
		case 2: /* all */
			m_term.clearRegion({CharPos{0, cursor.pos.y}, CharPos{tcols - 1, cursor.pos.y}});
			return;
		}
		return;
	case 'S': /* SU -- Scroll <n> line up */
		setDefault(arg0, 1);
		m_term.scrollUp(m_term.topScrollLimit(), arg0);
		return;
	case 'T': /* SD -- Scroll <n> line down */
		setDefault(arg0, 1);
		m_term.scrollDown(m_term.topScrollLimit(), arg0);
		return;
	case 'L': /* IL -- Insert <n> blank lines */
		setDefault(arg0, 1);
		m_term.insertBlankLine(arg0);
		return;
	case 'l': /* RM -- Reset Mode */
		m_term.setMode(m_priv, false, m_args);
		return;
	case 'M': /* DL -- Delete <n> lines */
		setDefault(arg0, 1);
		m_term.deleteLine(arg0);
		return;
	case 'X': /* ECH -- Erase <n> char */
		setDefault(arg0, 1);
		m_term.clearRegion({cursor.pos, cursor.pos.nextCol(arg0 -1)});
		return;
	case 'P': /* DCH -- Delete <n> char */
		setDefault(arg0, 1);
		m_term.deleteChar(arg0);
		return;
	case 'Z': /* CBT -- Cursor Backward Tabulation <n> tab stops */
		setDefault(arg0, 1);
		m_term.putTab(-arg0);
		return;
	case 'd': /* VPA -- Move to <row> */
		setDefault(arg0, 1);
		m_term.moveCursorAbsTo({cursor.pos.x, arg0 - 1});
		return;
	case 'h': /* SM -- Set terminal mode */
		m_term.setMode(m_priv, true, m_args);
		return;
	case 'm': /* SGR -- Terminal attribute (color) */
		m_term.setAttr(m_args);
		return;
	case 'n': /* DSR – Device Status Report (cursor position) */
		if (arg0 == 6) {
			auto buf = cosmos::sprintf("\033[%i;%iR", cursor.pos.y + 1, cursor.pos.x + 1);
			Nst::getTTY().write(buf.c_str(), buf.size(), 0);
		}
		return;
	case 'r': /* DECSTBM -- Set Scrolling Region */
		if (m_priv) {
			break;
		} else {
			setDefault(arg0, 1);
			ensureArg(1, trows);
			m_term.setScroll(arg0 - 1, m_args[1] - 1);
			m_term.moveCursorAbsTo({0, 0});
		}
		return;
	case 's': /* DECSC -- Save cursor position (ANSI.SYS) */
		m_term.cursorControl(Term::TCursor::Control::SAVE);
		return;
	case 'u': /* DECRC -- Restore cursor position (ANSI.SYS) */
		m_term.cursorControl(Term::TCursor::Control::LOAD);
		return;
	case ' ':
		switch (m_mode[1]) {
		case 'q': /* DECSCUSR -- Set Cursor Style */
			if (arg0 < 0 || static_cast<unsigned>(arg0) >= static_cast<unsigned>(CursorStyle::END))
				break;
			xsetcursor(static_cast<CursorStyle>(arg0));
			return;
		default:
			break;
		}
		break;
	}

	dump("erresc: unknown csi");
}

int CSIEscape::eschandle(unsigned char ascii) {
	auto &esc = m_term.getEscapeState();
	using Escape = Term::Escape;
	auto &cursor = m_term.getCursor();

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
	case 'k': /* old title set compatibility */
		m_term.strSequence(ascii);
		return 0;
	case 'n': /* LS2 -- Locking shift 2 */
	case 'o': /* LS3 -- Locking shift 3 */
		m_term.setCharset(2 + (ascii - 'n'));
		break;
	case '(': /* GZD4 -- set primary charset G0 */
	case ')': /* G1D4 -- set secondary charset G1 */
	case '*': /* G2D4 -- set tertiary charset G2 */
	case '+': /* G3D4 -- set quaternary charset G3 */
		m_term.setICharset(ascii - '(');
		esc.set(Escape::ALTCHARSET);
		return 0;
	case 'D': /* IND -- Linefeed */
		if (cursor.pos.y == m_term.bottomScrollLimit()) {
			m_term.scrollUp(m_term.topScrollLimit(), 1);
		} else {
			m_term.moveCursorTo(cursor.pos.nextLine());
		}
		break;
	case 'E': /* NEL -- Next line */
		m_term.putNewline(); /* always go to first col */
		break;
	case 'H': /* HTS -- Horizontal tab stop */
		m_term.setTabAtCursor(true);
		break;
	case 'M': /* RI -- Reverse index */
		if (cursor.pos.y == m_term.topScrollLimit()) {
			m_term.scrollDown(m_term.topScrollLimit(), 1);
		} else {
			m_term.moveCursorTo(cursor.pos.prevLine());
		}
		break;
	case 'Z': /* DECID -- Identify Terminal */
		Nst::getTTY().write(config::VTIDEN, strlen(config::VTIDEN), 0);
		break;
	case 'c': /* RIS -- Reset to initial state */
		m_term.reset();
		xsettitle(NULL);
		xloadcols();
		break;
	case '=': /* DECPAM -- Application keypad */
		xsetmode(true, WinMode::APPKEYPAD);
		break;
	case '>': /* DECPNM -- Normal keypad */
		xsetmode(false, WinMode::APPKEYPAD);
		break;
	case '7': /* DECSC -- Save Cursor */
		m_term.cursorControl(Term::TCursor::Control::SAVE);
		break;
	case '8': /* DECRC -- Restore Cursor */
		m_term.cursorControl(Term::TCursor::Control::LOAD);
		break;
	case '\\': /* ST -- String Terminator */
		if (esc.test(Escape::STR_END))
			m_strescseq.handle();
		break;
	default:
		std::cerr << "erresc: unknown sequence ESC " << cosmos::hexnum(ascii, 2)
			<< "'" << (std::isprint(ascii) ? ascii : '.') << "'\n";
		break;
	}
	return 1;
}

} // end ns
