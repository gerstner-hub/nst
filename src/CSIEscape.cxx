// C++
#include <cstring>
#include <iostream>

// cosmos
#include "cosmos/formatting.hxx"

// nst
#include "CSIEscape.hxx"
#include "nst_config.hxx"
#include "nst.hxx"
#include "Selection.hxx"
#include "Term.hxx"
#include "TTY.hxx"

namespace nst {

CSIEscape::CSIEscape(Nst &nst) :
		m_nst{nst} {
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
	int arg;
	size_t num_parsed;

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

	m_mode_suffix = std::string{it, m_str.end()};
	if (m_mode_suffix.empty())
		// make sure there is always a zero terminator available for
		// index based access
		m_mode_suffix.push_back('\0');

	// if no parameter is provided then a single zero default parameter is
	// implied acc. to spec
	if (m_args.empty())
		m_args.push_back(0);
}

void CSIEscape::dump(const std::string_view prefix) const {
	std::cerr << prefix << ": ESC[";

	auto get_repr = [](const char ch) -> std::string {
		switch(ch) {
			case '\n': return "\\n";
			case '\r': return "\\r";
			case 0x1b: return "\\e";
			default: return static_cast<std::string>(cosmos::HexNum{ch, 2});
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

void CSIEscape::setModeGeneric(const bool enable) {
	if (m_is_private_csi) {
		setPrivateMode(enable);
	} else {
		setMode(enable);
	}
}

void CSIEscape::setMode(const bool set) {

	auto &term = m_nst.term();

	for (const auto arg: m_args) {
		switch (arg) {
		case 0:  // Error (IGNORED)
			break;
		case 2:
			m_nst.wsys().setMode(WinMode::KBDLOCK, set);
			break;
		case 4:  // IRM -- Insertion-replacement
			term.setInsertMode(set);
			break;
		case 12: // SRM -- Send/Receive
			term.setEcho(!set);
			break;
		case 20: // LNM -- Linefeed/new line
			term.setCarriageReturn(set);
			break;
		default:
			std::cerr << "erresc: unknown set/reset mode " << arg << "\n";
			break;
		}
	}
}

void CSIEscape::setPrivateMode(const bool set) {

	auto &wsys = m_nst.wsys();
	auto &term = m_nst.term();

	for (const auto arg: m_args) {
		switch (arg) {
		case 1: // DECCKM -- Cursor key
			wsys.setMode(WinMode::APPCURSOR, set);
			break;
		case 5: // DECSCNM -- Reverse video
			wsys.setMode(WinMode::REVERSE, set);
			break;
		case 6: // DECOM -- Origin
			term.setCursorOriginMode(set);
			break;
		case 7: // DECAWM -- Auto wrap
			term.setAutoWrap(set);
			break;
		case 0:  // Error (IGNORED)
		case 2:  // DECANM -- ANSI/VT52 (IGNORED)
		case 3:  // DECCOLM -- Column  (IGNORED)
		case 4:  // DECSCLM -- Scroll (IGNORED)
		case 8:  // DECARM -- Auto repeat (IGNORED)
		case 18: // DECPFF -- Printer feed (IGNORED)
		case 19: // DECPEX -- Printer extent (IGNORED)
		case 42: // DECNRCM -- National characters (IGNORED)
		case 12: // att610 -- Start blinking cursor (IGNORED)
			break;
		case 25: // DECTCEM -- Text Cursor Enable Mode
			wsys.setMode(WinMode::HIDE_CURSOR, !set);
			break;
		case 9:    // X10 mouse compatibility mode
			wsys.setPointerMotion(false);
			wsys.setMode(WinMode::MOUSE, false);
			wsys.setMode(WinMode::MOUSEX10, set);
			break;
		case 1000: // report button press
			wsys.setPointerMotion(false);
			wsys.setMode(WinMode::MOUSE, false);
			wsys.setMode(WinMode::MOUSEBTN, set);
			break;
		case 1002: // report motion on button press
			wsys.setPointerMotion(false);
			wsys.setMode(WinMode::MOUSE, false);
			wsys.setMode(WinMode::MOUSEMOTION, set);
			break;
		case 1003: // enable all mouse motions
			wsys.setPointerMotion(set);
			wsys.setMode(WinMode::MOUSE, false);
			wsys.setMode(WinMode::MOUSEMANY, set);
			break;
		case 1004: // send focus events to TTY
			wsys.setMode(WinMode::FOCUS, set);
			break;
		case 1006: // extended mouse reporting mode
			wsys.setMode(WinMode::MOUSE_SGR, set);
			break;
		case 1034: // signify META key press by setting eight bit on input
			wsys.setMode(WinMode::EIGHT_BIT, set);
			break;
		case 1049: // swap screen & set/restore cursor as xterm
			/* FALLTHROUGH */
		case 47: // both stand for swap screen (XTerm), clearing it first
			/* FALLTHROUGH */
		case 1047:
			term.setAltScreen(set, /*with_cursor=*/arg == 1049);
			break;
		case 1048: // save/load cursor
			term.cursorControl(set ? CursorState::Control::SAVE : CursorState::Control::LOAD);
			break;
		case 2004: // bracketed paste mode
			wsys.setMode(WinMode::BRKT_PASTE, set);
			break;
		// Not implemented mouse modes. See below.
		case 1001: // mouse highlight mode; can hang the terminal by design when implemented.
		case 1005: // UTF-8 mouse mode; will confuse applications not supporting UTF-8 and luit.
		case 1015: // urxvt mangled mouse mode; incompatible
			   // and can be mistaken for other control
			   // codes.
			break;
		default:
			std::cerr << "erresc: unknown private set/reset mode " << arg << "\n";
			break;
		}
	}
}

void CSIEscape::process() {

	// spec reference: https://vt100.net/docs/vt510-rm/chapter4.html

	if (m_mode_suffix.empty())
		return;

	const auto arg0 = ensureArg(0, 0);
	auto &term = m_nst.term();
	const auto curpos = term.cursor().position();

	switch (m_mode_suffix.front()) {
	default:
		// ignore unsupported sequences
		break;
	case '@': // ICH -- Insert <n> blank char
		term.insertBlanksAfterCursor(arg0 ? arg0 : 1);
		return;
	case 'A': // CUU -- Cursor <n> Up
		term.moveCursorUp(arg0 ? arg0 : 1);
		return;
	case 'B': // CUD -- Cursor <n> Down
	case 'e': // VPR -- Cursor <n> Down
		term.moveCursorDown(arg0 ? arg0 : 1);
		return;
	case 'i': // MC -- Media Copy
		switch (arg0) {
			case 0: // print page
				term.dump();
				break;
			case 1: // print cursor line
				term.dumpCursorLine();
				break;
			case 2:
				m_nst.selection().dump();
				break;
			case 4: // reset autoprint mode
				term.setPrintMode(false);
				break;
			case 5: // set autoprint mode
				term.setPrintMode(true);
				break;
		}
		return;
	case 'c': // DA -- Device Attributes
		if (arg0 == 0)
			m_nst.tty().write(config::VT_IDENT, TTY::MayEcho{false});
		return;
	case 'b': // REP -- if last char is printable print it <n> more times
		term.repeatChar(arg0 ? arg0 : 1);
		return;
	case 'C': // CUF -- Cursor <n> Forward
	case 'a': // HPR -- Cursor <n> Forward
		term.moveCursorRight(arg0 ? arg0 : 1);
		return;
	case 'D': // CUB -- Cursor <n> Backward
		term.moveCursorLeft(arg0 ? arg0 : 1);
		return;
	case 'E': // CNL -- Cursor <n> Down and to first col
		term.moveCursorDown(arg0 ? arg0 : 1, Term::CarriageReturn{true});
		return;
	case 'F': // CPL -- Cursor <n> Up and to first col
		term.moveCursorUp(arg0 ? arg0 : 1, Term::CarriageReturn{true});
		return;
	case 'g': // TBC -- Tabulation clear
		switch (arg0) {
			case 0: // clear current tab stop
				term.setTabAtCursor(false);
				return;
			case 3: // clear all the tabs
				term.clearAllTabs();
				return;
			default:
				break;
		}
		break;
	case 'G': // CHA -- Move to <col>
	case '`': // HPA
		term.moveCursorToCol(arg0 ? arg0 - 1 : 0);
		return;
	case 'H':   // CUP -- Move to absolute <row> <col>
	case 'f': { // HVP
		const auto row = arg0 ? arg0 - 1 : 0;
		const auto col = ensureArg(1, 1) - 1;
		term.moveCursorAbsTo({col, row});
		return;
	}
	case 'I': // CHT -- Cursor Forward Tabulation <n> tab stops
		term.moveToNextTab(arg0 ? arg0 : 1);
		return;
	case 'J': // ED -- Clear screen
		switch (arg0) {
		case 0: // below. from cursor to end of display
			term.clearLinesBelowCursor();
			term.clearColsAfterCursor();
			return;
		case 1: // above: from start to cursor
			term.clearLinesAboveCursor();
			term.clearColsBeforeCursor();
			return;
		case 2: // whole display
		case 3: // including scroll-back buffer (which we don't have)
			term.clearScreen();
			return;
		default:
			break;
		}
		break;
	case 'K': // EL -- Clear line
		switch (arg0) {
		case 0: // right of cursor
			term.clearColsAfterCursor();
			return;
		case 1: // left of cursor
			term.clearColsBeforeCursor();
			return;
		case 2: // complete cursor line
			term.clearCursorLine();
			return;
		}
		return;
	case 'S': // SU -- Scroll <n> lines up
		term.scrollUp(arg0 ? arg0 : 1);
		return;
	case 'T': // SD -- Scroll <n> line down
		term.scrollDown(arg0 ? arg0 : 1);
		return;
	case 'L': // IL -- Insert <n> blank lines
		term.insertBlankLinesBelowCursor(arg0 ? arg0 : 1);
		return;
	case 'l': // RM -- Reset Mode
		setModeGeneric(/*enabled=*/false);
		return;
	case 'M': // DL -- Delete <n> lines
		term.deleteLinesBelowCursor(arg0 ? arg0 : 1);
		return;
	case 'X': // ECH -- Erase <n> char
		term.clearRegion({curpos, curpos.nextCol(arg0 ? arg0 - 1 : 0)});
		return;
	case 'P': // DCH -- Delete <n> char (backspace like, remaining cols are shifted left)
		term.deleteColsAfterCursor(arg0 ? arg0 : 1);
		return;
	case 'Z': // CBT -- Cursor Backward Tabulation <n> tab stops
		term.moveToPrevTab(arg0 ? arg0 : 1);
		return;
	case 'd': // VPA -- Move to <row>
		term.moveCursorAbsTo({curpos.x, arg0 ? arg0 - 1 : 0});
		return;
	case 'h': // SM -- Set terminal mode
		setModeGeneric(/*enabled=*/true);
		return;
	case 'm': // SGR -- Terminal attribute (color)
		if (!setCursorAttrs()) {
			dump("failed to set cursor attrs");
		}
		return;
	case 'n': // DSR – Device Status Report (cursor position)
		if (arg0 == 6) {
			auto buf = cosmos::sprintf("\033[%i;%iR", curpos.y + 1, curpos.x + 1);
			m_nst.tty().write(buf, TTY::MayEcho{false});
		}
		return;
	case 'r': // DECSTBM -- Set Scrolling Region
		if (m_is_private_csi) {
			break;
		} else {
			const auto start_row = arg0 ? arg0 : 1;
			const auto end_row = ensureArg(1, term.numRows());
			term.setScrollArea(LineSpan{start_row - 1, end_row - 1});
			term.moveCursorAbsTo({0, 0});
		}
		return;
	case 's': // DECSC -- Save cursor position (ANSI.SYS)
		term.cursorControl(CursorState::Control::SAVE);
		return;
	case 'u': // DECRC -- Restore cursor position (ANSI.SYS)
		term.cursorControl(CursorState::Control::LOAD);
		return;
	case ' ':
		// this comes with an intermediate character
		if (m_mode_suffix.size() < 2)
			break;

		switch (m_mode_suffix[1]) {
		case 'q': // DECSCUSR -- Set Cursor Style
			if (arg0 < 0 || static_cast<unsigned>(arg0) >= static_cast<unsigned>(CursorStyle::END))
				// cursor style out of range
				break;
			m_nst.wsys().setCursorStyle(CursorStyle{arg0});
			return;
		default:
			break;
		}
		break;
	}

	dump("erresc: unknown csi");
}

bool CSIEscape::setCursorAttrs() const {
	bool ret = true;
	auto &term = m_nst.term();

	for (auto it = m_args.begin(); it < m_args.end(); it++) {
		const auto attr = *it;
		switch (attr) {
		case 0:
			term.resetCursorAttrs();
			break;
		case 1:
			term.setCursorAttr(Attr::BOLD);
			break;
		case 2:
			term.setCursorAttr(Attr::FAINT);
			break;
		case 3:
			term.setCursorAttr(Attr::ITALIC);
			break;
		case 4:
			term.setCursorAttr(Attr::UNDERLINE);
			break;
		case 5: // slow blink
			/* FALLTHROUGH */
		case 6: // rapid blink
			term.setCursorAttr(Attr::BLINK);
			break;
		case 7:
			term.setCursorAttr(Attr::REVERSE);
			break;
		case 8:
			term.setCursorAttr(Attr::INVISIBLE);
			break;
		case 9:
			term.setCursorAttr(Attr::STRUCK);
			break;
		case 22:
			term.resetCursorAttr(Attr::BOLD);
			term.resetCursorAttr(Attr::FAINT);
			break;
		case 23:
			term.resetCursorAttr(Attr::ITALIC);
			break;
		case 24:
			term.resetCursorAttr(Attr::UNDERLINE);
			break;
		case 25:
			term.resetCursorAttr(Attr::BLINK);
			break;
		case 27:
			term.resetCursorAttr(Attr::REVERSE);
			break;
		case 28:
			term.resetCursorAttr(Attr::INVISIBLE);
			break;
		case 29:
			term.resetCursorAttr(Attr::STRUCK);
			break;
		case 38: {
			if (auto colidx = parseColor(++it); colidx != ColorIndex::INVALID)
				term.setCursorFgColor(colidx);
			break;
		}
		case 39:
			term.setCursorFgColor(config::DEFAULT_FG);
			break;
		case 48: {
			if (auto colidx = parseColor(++it); colidx != ColorIndex::INVALID)
				term.setCursorBgColor(colidx);
			break;
		}
		case 49:
			term.setCursorBgColor(config::DEFAULT_BG);
			break;
		default:
			if (!handleCursorColorSet(attr)) {
				std::cerr << "erresc(default): gfx attr " << attr << " unknown\n",
				ret = false;
			}
			break;
		} // end switch
	}

	return ret;
}

bool CSIEscape::handleCursorColorSet(const int attr) const {
	// this allows to calculate system color indices from CSI escape codes
	constexpr std::array<std::tuple<int, int, bool, int>, 4> RANGES{
		std::tuple{ 30,  37, true,  0}, // dim foreground colors
		          { 40,  47, false, 0}, // dim background colors
		          { 90,  97, true,  8}, // bright foreground colors
		          {100, 107, false, 8}  // bright background colors
	};

	auto &term = m_nst.term();

	for (const auto &range: RANGES) {
		const auto [start, end, is_fg, offset] = range;
		if (!cosmos::in_range(attr, start, end))
			continue;

		const ColorIndex idx{uint32_t(attr - start + offset)};

		if (is_fg)
			term.setCursorFgColor(idx);
		else
			term.setCursorBgColor(idx);

		return true;
	}

	return false;
}

ColorIndex CSIEscape::parseColor(std::vector<int>::const_iterator &it) const {

	const size_t num_pars = m_args.end() - it;

	auto toTrueColor = [](unsigned int r, unsigned int g, unsigned int b) -> ColorIndex {
		std::underlying_type<ColorIndex>::type raw = (r << 16) | (g << 8) | b;
		return to_true_color(ColorIndex{raw});
	};

	auto badPars = [num_pars]() {
		std::cerr << "erresc(38): Incorrect number of parameters (" << num_pars << ")\n";
		return ColorIndex::INVALID;
	};

	if (num_pars == 0)
		return badPars();

	const auto color_type = *it++;
	const auto left = num_pars - 1;

	switch (color_type) {
	case 2: { // direct color in RGB space
		if (left < 3)
			return badPars();

		const auto r = *it++;
		const auto g = *it++;
		const auto b = *it;

		if (r <= 255 && g <= 255 && b <= 255) {
			return toTrueColor(r, g, b);
		} else {
			std::cerr << "erresc: bad rgb color (" << r << "," << g << "," << b << ")" << std::endl;
			return ColorIndex::INVALID;
		}
	}
	case 5: { // indexed color
		if (left < 1)
			return badPars();

		const auto idx = *it >= 0 ? ColorIndex{uint32_t(*it)} : ColorIndex::INVALID;

		if (idx <= ColorIndex::END_256) {
			return idx;
		} else {
			std::cerr << "erresc: bad fg/bgcolor " << *it << std::endl;
			return ColorIndex::INVALID;
		}
	}
	case 0: // implementation defined (only foreground)
	case 1: // transparent
	case 3: // direct color in CMY space
	case 4: // direct color in CMYK space
	default:
		std::cerr << "erresc(38): gfx attr " << color_type << " unknown" << std::endl;
		return ColorIndex::INVALID;
	}
}

void CSIEscape::reportFocus(const bool in_focus) {
	auto &tty = m_nst.tty();
	if (in_focus)
		tty.write("\033[I", TTY::MayEcho{false});
	else
		tty.write("\033[O", TTY::MayEcho{false});
}

void CSIEscape::reportPaste(const bool started) {
	// this is the result of the BRKT_PASTE mode also enabled via escape
	// sequences
	auto &tty = m_nst.tty();
	if (started)
		tty.write("\033[200~", TTY::MayEcho{false});
	else
		tty.write("\033[201~", TTY::MayEcho{false});
}

} // end ns
