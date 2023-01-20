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

/// maximum number of parameters for a CSI sequence
constexpr size_t MAX_ARG_SIZE = 16;

} // end anon ns

CSIEscape::CSIEscape(Nst &nst) : m_nst(nst)  {
	m_args.reserve(MAX_ARG_SIZE);
	m_str.reserve(MAX_STR_SIZE);
}

int CSIEscape::ensureArg(size_t index, int defval) {
	const auto req_size = index + 1;

	if (m_args.size() < req_size)
		m_args.resize(req_size);

	auto &val = m_args[index];

	if (val <= 0) {
		val = defval;
	}

	return val;
}

void CSIEscape::parse() {
	m_args.clear();

	auto it = m_str.begin();
	int arg;
	size_t num_parsed;

	if (m_str.empty()) {
		return;
	} else if (m_str.front() == '?') {
		m_is_private_csi = true;
		it++;
	}

	// any missing values are usually defaulted to 0
	//
	// 0 is generally denoting a "default value" which can also be
	// something different depending on the command.
	//
	// a value generally cannot be negative from the spec's point of view.

	while (it < m_str.end()) {
		try {
			arg = std::stol(&(*it), &num_parsed);
			it += num_parsed;
		} catch(const std::invalid_argument &) {
			arg = 0;
		} catch(const std::out_of_range &) {
			arg = -1;
		}

		m_args.push_back(arg);

		if (*it != ';' || m_args.size() == MAX_ARG_SIZE)
			break;
		it++;
	}

	m_mode_suffix = std::string(it, m_str.end());
	if (m_mode_suffix.empty())
		// make sure there is always a zero terminator available for
		// index based access
		m_mode_suffix.push_back('\0');

	// if no parameter is provided then a single zero default parameter is
	// implied acc. to spec
	if (m_args.empty())
		m_args.push_back(0);
}

void CSIEscape::dump(const std::string_view &prefix) const {
	std::cerr << prefix << ": ESC[";

	auto get_repr = [](const char ch) -> std::string {
		switch(ch) {
			case '\n': return "\\n";
			case '\r': return "\\r";
			case 0x1b: return "\\e";
			default: return static_cast<std::string>(cosmos::hexnum(ch, 2));
		}
	};

	for (auto c: m_str) {
		if (std::isprint(c)) {
			std::cerr << c;
		} else {
			std::cerr << "(" << get_repr(c) << ")";
		}
	}

	std::cerr << "\n";
}

void CSIEscape::setMode(const bool enable) const {
	auto &term = m_nst.getTerm();

	if (m_is_private_csi) {
		term.setPrivateMode(enable, m_args);
	} else {
		term.setMode(       enable, m_args);
	}
}

void CSIEscape::process() {

	// spec reference: https://vt100.net/docs/vt510-rm/chapter4.html

	auto &term = m_nst.getTerm();
	const auto arg0 = m_args[0];
	const auto &curpos = term.getCursor().getPos();

	switch (m_mode_suffix.front()) {
	default:
		// ignore unsupported sequences
		break;
	case '@': /* ICH -- Insert <n> blank char */
		term.insertBlanksAfterCursor(arg0 ? arg0 : 1);
		return;
	case 'A': /* CUU -- Cursor <n> Up */
		term.moveCursorUp(arg0 ? arg0 : 1);
		return;
	case 'B': /* CUD -- Cursor <n> Down */
	case 'e': /* VPR --Cursor <n> Down */
		term.moveCursorDown(arg0 ? arg0 : 1);
		return;
	case 'i': /* MC -- Media Copy */
		switch (arg0) {
		case 0: // print page
			term.dump();
			break;
		case 1: // print cursor line
			term.dumpCursorLine();
			break;
		case 2:
			m_nst.getSelection().dump();
			break;
		case 4: // reset autoprint mode
			term.setPrintMode(false);
			break;
		case 5: // set autoprint mode
			term.setPrintMode(true);
			break;
		}
		return;
	case 'c': /* DA -- Device Attributes */
		if (arg0 == 0)
			m_nst.getTTY().write(config::VTIDEN, TTY::MayEcho(false));
		return;
	case 'b': /* REP -- if last char is printable print it <n> more times */
		term.repeatChar(arg0 ? arg0 : 1);
		return;
	case 'C': /* CUF -- Cursor <n> Forward */
	case 'a': /* HPR -- Cursor <n> Forward */
		term.moveCursorRight(arg0 ? arg0 : 1);
		return;
	case 'D': /* CUB -- Cursor <n> Backward */
		term.moveCursorLeft(arg0 ? arg0 : 1);
		return;
	case 'E': /* CNL -- Cursor <n> Down and first col */
		term.moveCursorDown(arg0 ? arg0 : 1, Term::CarriageReturn(true));
		return;
	case 'F': /* CPL -- Cursor <n> Up and first col */
		term.moveCursorUp(arg0 ? arg0 : 1, Term::CarriageReturn(true));
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
		term.moveCursorToCol(arg0 ? arg0 - 1 : 0);
		return;
	case 'H': /* CUP -- Move to absolute <row> <col> */
	case 'f': /* HVP */ {
		const auto row = arg0 ? arg0 - 1 : 0;
		const auto col = ensureArg(1, 1) - 1;
		term.moveCursorAbsTo({col, row});
		return;
	}
	case 'I': /* CHT -- Cursor Forward Tabulation <n> tab stops */
		term.moveToNextTab(arg0 ? arg0 : 1);
		return;
	case 'J': /* ED -- Clear screen */
		switch (arg0) {
		case 0: /* below. from cursor to end of display */
			term.clearLinesBelowCursor();
			term.clearColsAfterCursor();
			return;
		case 1: /* above: from start to cursor */
			term.clearLinesAboveCursor();
			term.clearColsBeforeCursor();
			return;
		case 2: /* whole display */
		case 3: /* including scroll-back buffer (which we don't have) */
			term.clearScreen();
			return;
		default:
			break;
		}
		break;
	case 'K': /* EL -- Clear line */
		switch (arg0) {
		case 0: /* right of cursor */
			term.clearColsAfterCursor();
			return;
		case 1: /* left of cursor */
			term.clearColsBeforeCursor();
			return;
		case 2: /* complete cursor line */
			term.clearCursorLine();
			return;
		}
		return;
	case 'S': /* SU -- Scroll <n> lines up */
		term.scrollUp(arg0 ? arg0 : 1);
		return;
	case 'T': /* SD -- Scroll <n> line down */
		term.scrollDown(arg0 ? arg0 : 1);
		return;
	case 'L': /* IL -- Insert <n> blank lines */
		term.insertBlankLinesBelowCursor(arg0 ? arg0 : 1);
		return;
	case 'l': /* RM -- Reset Mode */
		setMode(/*enabled=*/false);
		return;
	case 'M': /* DL -- Delete <n> lines */
		term.deleteLinesBelowCursor(arg0 ? arg0 : 1);
		return;
	case 'X': /* ECH -- Erase <n> char */
		term.clearRegion({curpos, curpos.nextCol(arg0 ? arg0 - 1 : 0)});
		return;
	case 'P': /* DCH -- Delete <n> char (backspace like, remaining cols are shifted left) */
		term.deleteColsAfterCursor(arg0 ? arg0 : 1);
		return;
	case 'Z': /* CBT -- Cursor Backward Tabulation <n> tab stops */
		term.moveToPrevTab(arg0 ? arg0 : 1);
		return;
	case 'd': /* VPA -- Move to <row> */
		term.moveCursorAbsTo({curpos.x, arg0 ? arg0 - 1 : 0});
		return;
	case 'h': /* SM -- Set terminal mode */
		setMode(/*enabled=*/true);
		return;
	case 'm': /* SGR -- Terminal attribute (color) */
		if (!term.setCursorAttrs(m_args)) {
			dump("failed to set cursor attrs");
		}
		return;
	case 'n': /* DSR – Device Status Report (cursor position) */
		if (arg0 == 6) {
			auto buf = cosmos::sprintf("\033[%i;%iR", curpos.y + 1, curpos.x + 1);
			m_nst.getTTY().write(buf, TTY::MayEcho(false));
		}
		return;
	case 'r': /* DECSTBM -- Set Scrolling Region */
		if (m_is_private_csi) {
			break;
		} else {
			const auto start_row = arg0 ? arg0 : 1;
			const auto end_row = ensureArg(1, term.getNumRows());
			term.setScrollArea(LineSpan{start_row - 1, end_row - 1});
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
		// this comes with an intermediate character
		if (m_mode_suffix.size() < 2)
			break;

		switch (m_mode_suffix[1]) {
		case 'q': /* DECSCUSR -- Set Cursor Style */
			if (arg0 < 0 || static_cast<unsigned>(arg0) >= static_cast<unsigned>(CursorStyle::END))
				// cursor style out of range
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

bool CSIEscape::handleEscape(const char ch) {
	auto &term = m_nst.getTerm();
	auto &state = term.getEscapeState();
	using Escape = Term::Escape;
	auto &x11 = m_nst.getX11();

	// TODO: separation of concerns regarding escape sequences parsing is
	// not well done, Term does too much of this on its own: some
	// functions even get passed in the raw m_args from us.
	//
	// Wrapping everything in enums is also not so great though ...
	//
	// also why are there non-CSI sequences here? what is the separation
	// wrt StringEscape?

	// for reference see `man 4 console_codes`

	// these are, apart from '[', non-CSI escape sequences
	switch (ch) {
	case '[':
		state.set(Escape::CSI);
		return false;
	case '#':
		state.set(Escape::TEST);
		return false;
	case '%': // character set selection
		state.set(Escape::UTF8);
		return false;
	case 'P': /* DCS -- Device Control String */
	case '_': /* APC -- Application Program Command */
	case '^': /* PM -- Privacy Message */
	case ']': /* OSC -- Operating System Command */
	case 'k': /* old title set compatibility */ {
		// hand over to StringEscape
		const auto esc_type = static_cast<StringEscape::Type>(ch);
		term.initStrSequence(esc_type);
		return false;
	}
	case 'n': /* LS2 -- Locking shift 2 */
	case 'o': /* LS3 -- Locking shift 3 */
		term.setCharset(2 + (ch - 'n'));
		break;
	case '(': /* GZD4 -- set primary charset G0 */
	case ')': /* G1D4 -- set secondary charset G1 */
	case '*': /* G2D4 -- set tertiary charset G2 */
	case '+': /* G3D4 -- set quaternary charset G3 */
		term.setEscCharset(ch - '(');
		state.set(Escape::ALTCHARSET);
		return false;
	case 'D': /* IND -- Linefeed */
		term.doLineFeed();
		break;
	case 'E': /* NEL -- Next line */
		term.moveToNewline(); /* always go to first col */
		break;
	case 'H': /* HTS -- Horizontal tab stop */
		term.setTabAtCursor(true);
		break;
	case 'M': /* RI -- Reverse index / linefeed */
		term.doReverseLineFeed();
		break;
	case 'Z': /* DECID -- Identify Terminal */
		m_nst.getTTY().write(config::VTIDEN, TTY::MayEcho(false));
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
		term.handleCommandTerminator();
		break;
	default:
		std::cerr << "erresc: unknown sequence ESC " << cosmos::hexnum(ch, 2)
			<< " '" << (std::isprint(ch) ? ch : '.') << "'\n";
		break;
	}

	return true;
}

} // end ns
