// C
#include <string.h>

// stdlib
#include <algorithm>
#include <cstring>
#include <iostream>

// cosmos
#include "cosmos/algs.hxx"

// nst
#include "codecs.hxx"
#include "nst_config.hxx"
#include "nst.hxx"
#include "Selection.hxx"
#include "StringEscape.hxx"
#include "Term.hxx"
#include "TTY.hxx"

using cosmos::in_range;

namespace {

bool isControlC0(const nst::Rune &c) {
	return c < 0x1f || c == 0x7f;
}

bool isControlC1(const nst::Rune &c) {
	return in_range(c, 0x80, 0x9f);
}

bool isControlChar(const nst::Rune &c) {
	return isControlC0(c) || isControlC1(c);
}

} // end anon ns

namespace nst {

typedef Glyph::Attr Attr;

Term::TCursor::TCursor() {
	attr.fg = config::DEFAULTFG;
	attr.bg = config::DEFAULTBG;
}

Term::Term(Nst &nst) :
	m_nst(nst),
	m_selection(nst.getSelection()),
	m_tty(nst.getTTY()),
	m_x11(nst.getX11()),
	m_allowaltscreen(config::ALLOWALTSCREEN),
	m_strescseq(nst),
	m_csiescseq(nst, m_strescseq) {}

void Term::init(const TermSize &tsize) {

	if (auto &cmdline = m_nst.getCmdline(); cmdline.use_alt_screen.isSet()) {
		m_allowaltscreen = cmdline.use_alt_screen.getValue();
	}

	resize(tsize);
	reset();
}

void Term::reset(void) {
	m_cursor = TCursor();

	clearAllTabs();
	for (auto i = config::TABSPACES; i < m_size.cols; i += config::TABSPACES)
		m_tabs[i] = true;
	resetScrollLimit();
	m_mode.set({Mode::WRAP, Mode::UTF8});
	m_trantbl.fill(Charset::USA);
	m_charset = 0;

	// reset main and alt screen
	for (size_t i = 0; i < 2; i++) {
		moveCursorTo(topLeft());
		cursorControl(TCursor::Control::SAVE);
		clearRegion({topLeft(), bottomRight()});
		swapScreen();
	}
}

void Term::setDirty(LineSpan span) {
	if (m_dirty_lines.empty())
		return;

	clamp(span);

	for (int i = span.top; i <= span.bottom; i++)
		m_dirty_lines[i] = true;
}

void Term::resize(const TermSize &new_size) {
	if (!new_size.valid()) {
		std::cerr << __FUNCTION__ << ": error resizing to " << new_size.cols << "x" << new_size.rows << "\n";
		return;
	}

	/*
	 * slide screen to keep cursor where we expect it - scrollUp would
	 * work here, but we can optimize to std::move because we're freeing
	 * the earlier lines.
	 *
	 * only do this if the new rows are smaller than the current cursor row
	 */
	if(const auto shift = m_cursor.pos.y - new_size.rows; shift > 0) {
		for (auto screen: {&m_screen, &m_alt_screen}) {
			std::move(screen->begin() + shift, screen->begin() + shift + new_size.rows, screen->begin());
		}
	}

	/* adjust dimensions of internal data structures */
	m_dirty_lines.resize(new_size.rows);
	m_tabs.resize(new_size.cols);

	for (auto screen: {&m_screen, &m_alt_screen}) {
		screen->resize(new_size.rows);

		/* resize each row to new width */
		for (auto &row: *screen) {
			row.resize(new_size.cols);
		}
	}

	// extend tab markers if we have more columns now
	if (new_size.cols > m_size.cols) {
		auto it = m_tabs.begin() + m_size.cols;

		// find last tab marker
		while (it > m_tabs.begin() && !*it)
			it--;
		for (it += config::TABSPACES; it < m_tabs.end(); it += config::TABSPACES)
			*it = true;
	}

	// remember the old size for updating new screen regions below
	const auto old_size = m_size;
	/* update terminal size */
	m_size = new_size;
	/* reset scrolling region */
	resetScrollLimit();
	/* make use of the clamping in moveCursorTo() to get a valid cursor position again */
	moveCursorTo(m_cursor.pos);

	/* Clear both screens (it makes dirty all lines) */
	auto saved_cursor = m_cursor;
	for (size_t i = 0; i < 2; i++) {
		// clear new cols if number of columns increased
		if (old_size.cols < new_size.cols && old_size.rows > 0) {
			clearRegion({CharPos{old_size.cols, 0}, CharPos{new_size.cols - 1, old_size.rows - 1}});
		}
		// clear new rows if number of rows increased
		if (old_size.rows < new_size.rows && old_size.cols > 0) {
			clearRegion({CharPos{0, old_size.rows}, bottomRight()});
		}
		swapScreen();
		cursorControl(TCursor::Control::LOAD);
	}
	m_cursor = saved_cursor;
}

void Term::clearRegion(Range range) {

	range.sanitize();
	range.clamp(bottomRight());

	for (auto y = range.begin.y; y <= range.end.y; y++) {
		m_dirty_lines[y] = true;
		for (auto x = range.begin.x; x <= range.end.x; x++) {
			const auto pos = CharPos{x, y};
			if (m_selection.isSelected(pos))
				m_selection.clear();
			auto &gp = getGlyphAt(pos);
			gp.clear(m_cursor.attr);
		}
	}
}

void Term::setScrollLimit(const LineSpan &span) {
	m_scroll_limit = span;
	clamp(m_scroll_limit);
	m_scroll_limit.sanitize();
}

void Term::moveCursorTo(CharPos pos) {
	LineSpan limit{0, m_size.rows - 1};

	if (m_cursor.state[TCursor::State::ORIGIN]) {
		limit = m_scroll_limit;
	}

	m_cursor.state.reset(TCursor::State::WRAPNEXT);
	clampCol(pos.x);
	pos.clampY(limit.top, limit.bottom);
	m_cursor.pos = pos;
}

/* for absolute user moves, when decom is set */
void Term::moveCursorAbsTo(CharPos pos) {
	if (m_cursor.state[TCursor::State::ORIGIN])
		pos.y += m_scroll_limit.top;
	moveCursorTo(pos);
}

void Term::swapScreen() {
	std::swap(m_screen, m_alt_screen);
	m_mode.flip(Mode::ALTSCREEN);
	setAllDirty();
}

void Term::cursorControl(const TCursor::Control &ctrl) {
	auto &cursor = m_mode[Mode::ALTSCREEN] ? m_cached_cursors[1] : m_cached_cursors[0];

	if (ctrl == TCursor::Control::SAVE) {
		cursor = m_cursor;
	} else if (ctrl == TCursor::Control::LOAD) {
		m_cursor = cursor;
		moveCursorTo(cursor.pos);
	}
}

int Term::getLineLen(int y) const {
	auto col = m_size.cols;
	const auto &line = m_screen[y];

	if (line[col-1].mode[Attr::WRAP])
		return col;

	while (col > 0 && line[col-1].u == ' ')
		--col;

	return col;
}

void Term::putTab(int n) {
	auto x = m_cursor.pos.x;

	if (n > 0) {
		while (x < m_size.cols && n--)
			for (++x; x < m_size.cols && !m_tabs[x]; ++x)
				/* nothing */ ;
	} else if (n < 0) {
		while (x > 0 && n++)
			for (--x; x > 0 && !m_tabs[x]; --x)
				/* nothing */ ;
	}

	m_cursor.pos.x = limitCol(x);
}

void Term::putNewline(bool first_col) {
	CharPos new_pos = m_cursor.pos;

	if (first_col)
		new_pos.x = 0;

	if (new_pos.y == m_scroll_limit.bottom) {
		scrollUp(m_scroll_limit.top, 1);
	} else {
		new_pos.y++;
	}

	moveCursorTo(new_pos);
}

void Term::deleteChar(int n) {
	n = std::clamp(n, 0, m_size.cols - m_cursor.pos.x);

	const int dst = m_cursor.pos.x;
	const int src = m_cursor.pos.x + n;
	const int size = m_size.cols - src;
	auto &line = m_screen[m_cursor.pos.y];

	// slide remaining line content n characters to the left
	std::memmove(&line[dst], &line[src], size * sizeof(Glyph));
	// clear n characters at end of line
	clearRegion({CharPos{m_size.cols - n, m_cursor.pos.y}, CharPos{m_size.cols - 1, m_cursor.pos.y}});
}

void Term::deleteLine(int n) {
	if (in_range(m_cursor.pos.y, m_scroll_limit.top, m_scroll_limit.bottom))
		scrollUp(m_cursor.pos.y, n);
}

void Term::insertBlank(int n) {
	n = std::clamp(n, 0, m_size.cols - m_cursor.pos.x);

	const int dst = m_cursor.pos.x + n;
	const int src = m_cursor.pos.x;
	const int size = m_size.cols - dst;
	auto &line = m_screen[m_cursor.pos.y];

	std::memmove(&line[dst], &line[src], size * sizeof(Glyph));
	clearRegion({CharPos{src, m_cursor.pos.y}, CharPos{dst - 1, m_cursor.pos.y}});
}

void Term::insertBlankLine(int n) {
	if (in_range(m_cursor.pos.y, m_scroll_limit.top, m_scroll_limit.bottom))
		scrollDown(m_cursor.pos.y, n);
}

void Term::scrollDown(int orig, int n) {
	n = std::clamp(n, 0, m_scroll_limit.bottom - orig + 1);

	setDirty(LineSpan{orig, m_scroll_limit.bottom - n});
	clearRegion({CharPos{0, m_scroll_limit.bottom - n + 1}, CharPos{m_size.cols - 1, m_scroll_limit.bottom}});

	for (int i = m_scroll_limit.bottom; i >= orig+n; i--) {
		std::swap(m_screen[i], m_screen[i-n]);
	}

	m_selection.scroll(orig, n);
}

void Term::scrollUp(int orig, int n) {
	n = std::clamp(n, 0, m_scroll_limit.bottom - orig + 1);

	setDirty(LineSpan{orig+n, m_scroll_limit.bottom});
	clearRegion({CharPos{0, orig}, CharPos{m_size.cols - 1, orig+n-1}});

	for (int i = orig; i <= m_scroll_limit.bottom - n; i++) {
		std::swap(m_screen[i], m_screen[i+n]);
	}

	m_selection.scroll(orig, -n);
}

void Term::setAttr(const std::vector<int> &attr) {
	for (size_t i = 0; i < attr.size(); i++) {
		switch (attr[i]) {
		case 0:
			m_cursor.attr.mode.reset({
				Attr::BOLD,
				Attr::FAINT,
				Attr::ITALIC,
				Attr::UNDERLINE,
				Attr::BLINK,
				Attr::REVERSE,
				Attr::INVISIBLE,
				Attr::STRUCK
			});
			m_cursor.attr.fg = config::DEFAULTFG;
			m_cursor.attr.bg = config::DEFAULTBG;
			break;
		case 1:
			m_cursor.attr.mode.set(Attr::BOLD);
			break;
		case 2:
			m_cursor.attr.mode.set(Attr::FAINT);
			break;
		case 3:
			m_cursor.attr.mode.set(Attr::ITALIC);
			break;
		case 4:
			m_cursor.attr.mode.set(Attr::UNDERLINE);
			break;
		case 5: /* slow blink */
			/* FALLTHROUGH */
		case 6: /* rapid blink */
			m_cursor.attr.mode.set(Attr::BLINK);
			break;
		case 7:
			m_cursor.attr.mode.set(Attr::REVERSE);
			break;
		case 8:
			m_cursor.attr.mode.set(Attr::INVISIBLE);
			break;
		case 9:
			m_cursor.attr.mode.set(Attr::STRUCK);
			break;
		case 22:
			m_cursor.attr.mode.reset({Attr::BOLD, Attr::FAINT});
			break;
		case 23:
			m_cursor.attr.mode.reset(Attr::ITALIC);
			break;
		case 24:
			m_cursor.attr.mode.reset(Attr::UNDERLINE);
			break;
		case 25:
			m_cursor.attr.mode.reset(Attr::BLINK);
			break;
		case 27:
			m_cursor.attr.mode.reset(Attr::REVERSE);
			break;
		case 28:
			m_cursor.attr.mode.reset(Attr::INVISIBLE);
			break;
		case 29:
			m_cursor.attr.mode.reset(Attr::STRUCK);
			break;
		case 38:
			if (auto idx = defcolor(attr, i); idx >= 0)
				m_cursor.attr.fg = idx;
			break;
		case 39:
			m_cursor.attr.fg = config::DEFAULTFG;
			break;
		case 48:
			if (auto idx = defcolor(attr, i); idx >= 0)
				m_cursor.attr.bg = idx;
			break;
		case 49:
			m_cursor.attr.bg = config::DEFAULTBG;
			break;
		default:
			if (in_range(attr[i], 30, 37)) {
				m_cursor.attr.fg = attr[i] - 30;
			} else if (in_range(attr[i], 40, 47)) {
				m_cursor.attr.bg = attr[i] - 40;
			} else if (in_range(attr[i], 90, 97)) {
				m_cursor.attr.fg = attr[i] - 90 + 8;
			} else if (in_range(attr[i], 100, 107)) {
				m_cursor.attr.bg = attr[i] - 100 + 8;
			} else {
				std::cerr << "erresc(default): gfx attr " << attr[i] << " unknown\n",
				m_csiescseq.dump("");
			}
			break;
		}
	}
}

int32_t Term::defcolor(const std::vector<int> &attr, size_t &npar) {
	// TODO: could this be a possible out of bound access here if attr.size() == 1?
	switch (attr[npar + 1]) {
	case 2: /* direct color in RGB space */ {
		if (npar + 4 >= attr.size()) {
			std::cerr << "erresc(38): Incorrect number of parameters (" << npar << ")\n";
			break;
		}
		const unsigned int r = attr[npar + 2];
		const unsigned int g = attr[npar + 3];
		const unsigned int b = attr[npar + 4];
		npar += 4;
		if (r > 255 || g > 255 || b > 255) {
			std::cerr << "erresc: bad rgb color (" << r << "," << g << "," << b << ")" << std::endl;
			break;
		}

		return toTrueColor(r, g, b);
	}
	case 5: /* indexed color */
		if (npar + 2 >= attr.size()) {
			std::cerr << "erresc(38): Incorrect number of parameters (" << npar << ")" << std::endl;
			break;
		}
		npar += 2;
		if (!in_range(attr[npar], 0, 255)) {
			std::cerr << "erresc: bad fgcolor " << attr[npar] << std::endl;
			break;
		}

		return attr[npar];
	case 0: /* implementation defined (only foreground) */
	case 1: /* transparent */
	case 3: /* direct color in CMY space */
	case 4: /* direct color in CMYK space */
	default:
		std::cerr << "erresc(38): gfx attr " << attr[npar] << " unknown" << std::endl;
		break;
	}

	return -1;
}

void Term::setMode(bool priv, bool set, const std::vector<int> &args) {
	for (const auto arg: args) {
		if (priv) {
			switch (arg) {
			case 1: /* DECCKM -- Cursor key */
				m_x11.setMode(WinMode::APPCURSOR, set);
				break;
			case 5: /* DECSCNM -- Reverse video */
				m_x11.setMode(WinMode::REVERSE, set);
				break;
			case 6: /* DECOM -- Origin */
				m_cursor.state.set(TCursor::State::ORIGIN, set);
				moveCursorAbsTo(topLeft());
				break;
			case 7: /* DECAWM -- Auto wrap */
				m_mode.set(Mode::WRAP, set);
				break;
			case 0:  /* Error (IGNORED) */
			case 2:  /* DECANM -- ANSI/VT52 (IGNORED) */
			case 3:  /* DECCOLM -- Column  (IGNORED) */
			case 4:  /* DECSCLM -- Scroll (IGNORED) */
			case 8:  /* DECARM -- Auto repeat (IGNORED) */
			case 18: /* DECPFF -- Printer feed (IGNORED) */
			case 19: /* DECPEX -- Printer extent (IGNORED) */
			case 42: /* DECNRCM -- National characters (IGNORED) */
			case 12: /* att610 -- Start blinking cursor (IGNORED) */
				break;
			case 25: /* DECTCEM -- Text Cursor Enable Mode */
				m_x11.setMode(WinMode::HIDE, !set);
				break;
			case 9:    /* X10 mouse compatibility mode */
				m_x11.setPointerMotion(false);
				m_x11.setMode(WinMode::MOUSE, false);
				m_x11.setMode(WinMode::MOUSEX10, set);
				break;
			case 1000: /* 1000: report button press */
				m_x11.setPointerMotion(false);
				m_x11.setMode(WinMode::MOUSE, false);
				m_x11.setMode(WinMode::MOUSEBTN, set);
				break;
			case 1002: /* 1002: report motion on button press */
				m_x11.setPointerMotion(false);
				m_x11.setMode(WinMode::MOUSE, false);
				m_x11.setMode(WinMode::MOUSEMOTION, set);
				break;
			case 1003: /* 1003: enable all mouse motions */
				m_x11.setPointerMotion(set);
				m_x11.setMode(WinMode::MOUSE, false);
				m_x11.setMode(WinMode::MOUSEMANY, set);
				break;
			case 1004: /* 1004: send focus events to tty */
				m_x11.setMode(WinMode::FOCUS, set);
				break;
			case 1006: /* 1006: extended reporting mode */
				m_x11.setMode(WinMode::MOUSESGR, set);
				break;
			case 1034:
				m_x11.setMode(WinMode::EIGHT_BIT, set);
				break;
			case 1049: /* swap screen & set/restore cursor as xterm */
				if (!m_allowaltscreen)
					break;
				cursorControl((set) ? TCursor::Control::SAVE : TCursor::Control::LOAD);
				/* FALLTHROUGH */
			case 47: /* swap screen */
			case 1047: {
				if (!m_allowaltscreen)
					break;
				const auto is_alt = m_mode[Mode::ALTSCREEN];
				if (is_alt) {
					clearRegion({topLeft(), bottomRight()});
				}
				if (set ^ is_alt) /* set is always 1 or 0 */
					swapScreen();
				if (arg != 1049)
					break;
			}
				/* FALLTHROUGH */
			case 1048:
				cursorControl(set ? TCursor::Control::SAVE : TCursor::Control::LOAD);
				break;
			case 2004: /* 2004: bracketed paste mode */
				m_x11.setMode(WinMode::BRCKTPASTE, set);
				break;
			/* Not implemented mouse modes. See comments there. */
			case 1001: /* mouse highlight mode; can hang the
				      terminal by design when implemented. */
			case 1005: /* UTF-8 mouse mode; will confuse
				      applications not supporting UTF-8
				      and luit. */
			case 1015: /* urxvt mangled mouse mode; incompatible
				      and can be mistaken for other control
				      codes. */
				break;
			default:
				std::cerr << "erresc: unknown private set/reset mode " << arg << "\n";
				break;
			}
		} else {
			switch (arg) {
			case 0:  /* Error (IGNORED) */
				break;
			case 2:
				m_x11.setMode(WinMode::KBDLOCK, set);
				break;
			case 4:  /* IRM -- Insertion-replacement */
				m_mode.set(Mode::INSERT, set);
				break;
			case 12: /* SRM -- Send/Receive */
				m_mode.set(Mode::TECHO, !set);
				break;
			case 20: /* LNM -- Linefeed/new line */
				m_mode.set(Mode::CRLF, set);
				break;
			default:
				std::cerr << "erresc: unknown set/reset mode " << arg << "\n";
				break;
			}
		}
	}
}

void Term::dumpLine(size_t n) const {
	char buf[utf8::UTF_SIZE];

	auto bp = m_screen[n].begin();
	auto end = bp + std::min(getLineLen(n), m_size.cols) - 1;
	if (bp != end || bp->u != ' ') {
		for ( ; bp <= end; ++bp)
			m_tty.printToIoFile(buf, utf8::encode(bp->u, buf));
	}
	m_tty.printToIoFile("\n", 1);
}

bool Term::existsBlinkingGlyph() const {
	// NOTE: this test could probably be cheaper by keeping track of this
	// attribute when changing glyphs.

	for (int y = 0; y < m_size.rows - 1; y++) {
		for (int x = 0; x < m_size.cols-1; x++) {
			if (m_screen[y][x].mode[Attr::BLINK])
				return true;
		}
	}

	return false;
}

void Term::setDirtyByAttr(const Glyph::Attr &attr) {
	for (int y = 0; y < m_size.rows - 1; y++) {
		for (int x = 0; x < m_size.cols-1; x++) {
			if (m_screen[y][x].mode[attr]) {
				setDirty(LineSpan{y, y});
				break;
			}
		}
	}
}

void Term::drawRegion(const Range &range) const {
	for (int y = range.begin.y; y <= range.end.y; y++) {
		if (!m_dirty_lines[y])
			continue;

		m_dirty_lines[y] = false;
		m_x11.drawLine(m_screen[y], CharPos{range.begin.x, y}, range.width());
	}
}

void Term::draw() {
	if (!m_x11.canDraw())
		return;

	// TODO: pretty confusing logic, further comments or improved
	// readability required

	auto old_cursor_pos = m_old_cursor_pos;
	int old_cx = m_cursor.pos.x;

	/* adjust cursor position */
	clampToScreen(m_old_cursor_pos);

	if (getGlyphAt(m_old_cursor_pos).mode[Attr::WDUMMY])
		m_old_cursor_pos.x--;
	if (m_screen[m_cursor.pos.y][old_cx].mode[Attr::WDUMMY])
		old_cx--;

	drawScreen();
	const auto new_pos = CharPos{old_cx, m_cursor.pos.y};
	m_x11.clearCursor(m_old_cursor_pos, getGlyphAt(m_old_cursor_pos));
	m_x11.drawCursor(new_pos, getGlyphAt(new_pos));

	m_old_cursor_pos = new_pos;
	m_x11.finishDraw();
	if (m_old_cursor_pos != old_cursor_pos)
		m_x11.getInput().setSpot(m_old_cursor_pos);
}

void Term::strSequence(unsigned char ch) {
	switch (ch) {
	case 0x90:   /* DCS -- Device Control String */
		ch = 'P';
		break;
	case 0x9f:   /* APC -- Application Program Command */
		ch = '_';
		break;
	case 0x9e:   /* PM -- Privacy Message */
		ch = '^';
		break;
	case 0x9d:   /* OSC -- Operating System Command */
		ch = ']';
		break;
	}

	m_strescseq.reset(ch);
	m_esc_state.set(Escape::STR);
}

void Term::setChar(Rune u, const Glyph &attr, const CharPos &pos) {
	constexpr const char *VT100_0[0x7e - 0x41 + 1] = { /* 0x41 - 0x7e */
		"↑", "↓", "→", "←", "█", "▚", "☃", /* A - G */
		0, 0, 0, 0, 0, 0, 0, 0, /* H - O */
		0, 0, 0, 0, 0, 0, 0, 0, /* P - W */
		0, 0, 0, 0, 0, 0, 0, " ", /* X - _ */
		"◆", "▒", "␉", "␌", "␍", "␊", "°", "±", /* ` - g */
		"␤", "␋", "┘", "┐", "┌", "└", "┼", "⎺", /* h - o */
		"⎻", "─", "⎼", "⎽", "├", "┤", "┴", "┬", /* p - w */
		"│", "≤", "≥", "π", "≠", "£", "·", /* x - ~ */
	};

	/*
	 * The table is proudly stolen from rxvt.
	 */
	if (m_trantbl[m_charset] == Charset::GRAPHIC0 && in_range(u, 0x41, 0x7e) && VT100_0[u - 0x41])
		utf8::decode(VT100_0[u - 0x41], &u, utf8::UTF_SIZE);

	auto &glyph = getGlyphAt(pos);

	if (glyph.mode[Attr::WIDE]) {
		if (pos.x+1 < m_size.cols) {
			auto &next_glyph = getGlyphAt(pos.nextCol());
			next_glyph.u = ' ';
			next_glyph.mode.reset(Attr::WDUMMY);
		}
	} else if (glyph.mode[Attr::WDUMMY]) {
		auto &prev_glyph = getGlyphAt(pos.prevCol());
		prev_glyph.u = ' ';
		prev_glyph.mode.reset(Attr::WIDE);
	}

	m_dirty_lines[pos.y] = true;
	glyph = attr;
	glyph.u = u;
}

void Term::setDefTran(char ascii) {
	constexpr char cs[] = "0B";
	constexpr Charset vcs[] = {Charset::GRAPHIC0, Charset::USA};
	const char *p = strchr(cs, ascii);

	if (p == nullptr) {
		std::cerr << "esc unhandled charset: ESC ( " << ascii << "\n";
	} else {
		m_trantbl[m_icharset] = vcs[p - cs];
	}
}

void Term::decTest(char ch) {
	if (ch == '8') { /* DEC screen alignment test. */
		for (int x = 0; x < m_size.cols; ++x) {
			for (int y = 0; y < m_size.rows; ++y)
				setChar('E', m_cursor.attr, CharPos{x, y});
		}
	}
}

void Term::handleControlCode(unsigned char ascii) {
	switch (ascii) {
	case '\t':   /* HT */
		putTab(1);
		return;
	case '\b':   /* BS */
		moveCursorTo(m_cursor.pos.prevCol());
		return;
	case '\r':   /* CR */
		moveCursorTo({0, m_cursor.pos.y});
		return;
	case '\f':   /* LF */
	case '\v':   /* VT */
	case '\n':   /* LF */
		/* go to first col if the mode is set */
		putNewline(m_mode[Mode::CRLF]);
		return;
	case '\a':   /* BEL */
		if (m_esc_state[Escape::STR_END]) {
			/* backwards compatibility to xterm */
			m_strescseq.handle();
		} else {
			m_x11.ringBell();
		}
		break;
	case '\033': /* ESC */
		m_csiescseq.reset();
		m_esc_state.reset({Escape::CSI, Escape::ALTCHARSET, Escape::TEST});
		m_esc_state.set(Escape::START);
		return;
	case '\016': /* SO (LS1 -- Locking shift 1) */
	case '\017': /* SI (LS0 -- Locking shift 0) */
		m_charset = 1 - (ascii - '\016');
		return;
	case '\032': /* SUB */
		setChar('?', m_cursor.attr, m_cursor.pos);
		/* FALLTHROUGH */
	case '\030': /* CAN */
		m_csiescseq.reset();
		break;
	case '\005': /* ENQ (IGNORED) */
	case '\000': /* NUL (IGNORED) */
	case '\021': /* XON (IGNORED) */
	case '\023': /* XOFF (IGNORED) */
	case 0177:   /* DEL (IGNORED) */
		return;
	case 0x80:   /* TODO: PAD */
	case 0x81:   /* TODO: HOP */
	case 0x82:   /* TODO: BPH */
	case 0x83:   /* TODO: NBH */
	case 0x84:   /* TODO: IND */
		break;
	case 0x85:   /* NEL -- Next line */
		putNewline(); /* always go to first col */
		break;
	case 0x86:   /* TODO: SSA */
	case 0x87:   /* TODO: ESA */
		break;
	case 0x88:   /* HTS -- Horizontal tab stop */
		m_tabs[m_cursor.pos.x] = true;
		break;
	case 0x89:   /* TODO: HTJ */
	case 0x8a:   /* TODO: VTS */
	case 0x8b:   /* TODO: PLD */
	case 0x8c:   /* TODO: PLU */
	case 0x8d:   /* TODO: RI */
	case 0x8e:   /* TODO: SS2 */
	case 0x8f:   /* TODO: SS3 */
	case 0x91:   /* TODO: PU1 */
	case 0x92:   /* TODO: PU2 */
	case 0x93:   /* TODO: STS */
	case 0x94:   /* TODO: CCH */
	case 0x95:   /* TODO: MW */
	case 0x96:   /* TODO: SPA */
	case 0x97:   /* TODO: EPA */
	case 0x98:   /* TODO: SOS */
	case 0x99:   /* TODO: SGCI */
		break;
	case 0x9a:   /* DECID -- Identify Terminal */
		m_tty.write(config::VTIDEN, strlen(config::VTIDEN), 0);
		break;
	case 0x9b:   /* TODO: CSI */
	case 0x9c:   /* TODO: ST */
		break;
	case 0x90:   /* DCS -- Device Control String */
	case 0x9d:   /* OSC -- Operating System Command */
	case 0x9e:   /* PM -- Privacy Message */
	case 0x9f:   /* APC -- Application Program Command */
		strSequence(ascii);
		return;
	}
	/* only CAN, SUB, \a and C1 chars interrupt a sequence */
	resetStringEscape();
}

void Term::putChar(Rune u) {
	char ch[utf8::UTF_SIZE];
	int width;
	size_t len;

	const int is_control = isControlChar(u);
	if (u < 127 || !m_mode[Mode::UTF8]) {
		ch[0] = u;
		width = len = 1;
	} else {
		len = utf8::encode(u, ch);
		if (!is_control && (width = wcwidth(u)) == -1)
			width = 1;
	}

	if (m_mode[Mode::PRINT])
		m_tty.printToIoFile(ch, len);

	/*
	 * STR sequence must be checked before anything else because it uses
	 * all following characters until it receives a ESC, a SUB, a ST or
	 * any other C1 control character.
	 */
	if (m_esc_state[Escape::STR]) {
		if (u == '\a' || u == 030 || u == 032 || u == 033 || isControlC1(u)) {
			m_esc_state.reset({Escape::START, Escape::STR});
			m_esc_state.set(Escape::STR_END);
		} else {
			m_strescseq.add(ch, len);
			return;
		}
	}

	/*
	 * Actions of control codes must be performed as soon they arrive
	 * because they can be embedded inside a control sequence, and they
	 * must not cause conflicts with sequences.
	 */
	if (is_control) {
		handleControlCode(u);
		/*
		 * control codes are not shown ever
		 */
		if (m_esc_state.none())
			m_last_char = 0;
		return;
	} else if (m_esc_state[Escape::START]) {
		if (m_esc_state[Escape::CSI]) {
			const bool max_reached = m_csiescseq.add(u);
			if (max_reached || in_range(u, 0x40, 0x7E)) {
				m_esc_state.reset();
				m_csiescseq.parse();
				m_csiescseq.handle();
			}
			return;
		} else if (m_esc_state[Escape::UTF8]) {
			switch (static_cast<char>(u)) {
			case 'G':
				m_mode.set(Mode::UTF8);
				break;
			case '@':
				m_mode.reset(Mode::UTF8);
				break;
			}
		} else if (m_esc_state[Escape::ALTCHARSET]) {
			setDefTran(u);
		} else if (m_esc_state[Escape::TEST]) {
			decTest(u);
		} else if (!m_csiescseq.eschandle(u)) {
			return;
			/* sequence already finished */
		}

		m_esc_state.reset();

		/*
		 * All characters which form part of a sequence are not
		 * printed
		 */
		return;
	}

	if (m_selection.isSelected(m_cursor.pos))
		m_selection.clear();

	Glyph *gp = &getGlyphAt(m_cursor.pos);
	if (m_mode[Mode::WRAP] && m_cursor.state[TCursor::State::WRAPNEXT]) {
		gp->mode.set(Attr::WRAP);
		putNewline();
		gp = &getGlyphAt(m_cursor.pos);
	}

	if (m_mode[Mode::INSERT] && m_cursor.pos.x + width < m_size.cols)
		std::memmove(gp+width, gp, (m_size.cols - m_cursor.pos.x - width) * sizeof(Glyph));

	if (m_cursor.pos.x + width > m_size.cols) {
		putNewline();
		gp = &getGlyphAt(m_cursor.pos);
	}

	setChar(u, m_cursor.attr, m_cursor.pos);
	m_last_char = u;

	if (width == 2) {
		gp->mode.set(Attr::WIDE);
		if (m_cursor.pos.x + 1 < m_size.cols) {
			if (gp[1].mode[Attr::WIDE] && m_cursor.pos.x + 2 < m_size.cols) {
				gp[2].u = ' ';
				gp[2].mode.reset(Attr::WDUMMY);
			}
			gp[1].u = '\0';
			gp[1].mode.limit(Attr::WDUMMY);
		}
	}

	if (m_cursor.pos.x + width < m_size.cols) {
		moveCursorTo(m_cursor.pos.nextCol(width));
	} else {
		m_cursor.state.set(TCursor::State::WRAPNEXT);
	}
}

size_t Term::write(const char *buf, const size_t buflen, const bool show_ctrl) {
	Rune u;
	size_t charsize = 0, n;

	for (n = 0; n < buflen; n += charsize) {
		if (m_mode[Mode::UTF8]) {
			/* process a complete utf8 char */
			charsize = utf8::decode(buf + n, &u, buflen - n);
			if (charsize == 0)
				break;
		} else {
			u = buf[n] & 0xFF;
			charsize = 1;
		}

		if (show_ctrl && isControlChar(u)) {
			if (u & 0x80) {
				u &= 0x7f;
				putChar('^');
				putChar('[');
			} else if (u != '\n' && u != '\r' && u != '\t') {
				u ^= 0x40;
				putChar('^');
			}
		}
		putChar(u);
	}
	return n;
}

} // end ns
