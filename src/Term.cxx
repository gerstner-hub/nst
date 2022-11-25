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
#include "nst_config.h"
#include "Selection.hxx"
#include "StringEscape.hxx"
#include "Term.hxx"
#include "TTY.hxx"
#include "nst.hxx"
#include "win.h"

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

}

namespace nst {

typedef Glyph::Attr Attr;

Term::TCursor::TCursor() {
	attr.fg = config::DEFAULTFG;
	attr.bg = config::DEFAULTBG;
}


void Term::init(int cols, int rows) {
	resize(cols, rows);
	reset();
}

void Term::reset(void) {
	m_cursor = TCursor();

	clearAllTabs();
	for (size_t i = config::TABSPACES; (int)i < m_cols; i += config::TABSPACES)
		m_tabs[i] = true;
	m_top_scroll = 0;
	m_bottom_scroll = m_rows - 1;
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

void Term::setDirty(int top, int bot) {
	if (m_dirty_lines.empty())
		return;

	top = std::clamp(top, 0, m_rows-1);
	bot = std::clamp(bot, 0, m_rows-1);

	for (int i = top; i <= bot; i++)
		m_dirty_lines[i] = true;
}

void Term::resize(int new_cols, int new_rows) {
	if (new_cols < 1 || new_rows < 1) {
		fprintf(stderr, "%s: error resizing to %dx%d\n", __FUNCTION__, new_cols, new_rows);
		return;
	}

	const int minrow = std::min(new_rows, m_rows);
	const int mincol = std::min(new_cols, m_cols);

	/*
	 * slide screen to keep cursor where we expect it - scrollUp would
	 * work here, but we can optimize to std::move because we're freeing
	 * the earlier lines.
	 *
	 * only do this if the new rows are smaller than the current cursow
	 * row
	 */
	const auto shift = m_cursor.pos.y - new_rows;
	if (shift > 0) {
		std::move(m_screen.begin() + shift, m_screen.begin() + shift + new_rows, m_screen.begin());
		std::move(m_alt_screen.begin() + shift, m_alt_screen.begin() + shift + new_rows, m_alt_screen.begin());
	}

	/* allocate new memory with new height */
	m_screen.resize(new_rows);
	m_alt_screen.resize(new_rows);
	m_dirty_lines.resize(new_rows);
	m_tabs.resize(new_cols);

	/* resize each row to new width */
	for (int i = 0; i < new_rows; i++) {
		m_screen[i].resize(new_cols);
		m_alt_screen[i].resize(new_cols);
	}

	// extend tab markers if we have more columns now
	if (new_cols > m_cols) {
		auto it = m_tabs.begin() + m_cols;

		// find last tab marker
		while (it > m_tabs.begin() && !*it)
			it--;
		for (it += config::TABSPACES; it < m_tabs.end(); it += config::TABSPACES)
			*it = true;
	}
	/* update terminal size */
	m_cols = new_cols;
	m_rows = new_rows;
	/* reset scrolling region */
	setScroll(0, m_rows-1);
	/* make use of the clamping in moveCursorTo() to get a valid cursor position
	 * again */
	moveCursorTo(m_cursor.pos);
	/* Clear both screens (it makes dirty all lines) */
	auto saved_cursor = m_cursor;
	for (size_t i = 0; i < 2; i++) {
		if (mincol < new_cols && 0 < minrow) {
			clearRegion({CharPos{mincol, 0}, CharPos{new_cols - 1, minrow - 1}});
		}
		if (0 < new_cols && minrow < new_rows) {
			clearRegion({CharPos{0, minrow}, CharPos{new_cols - 1, new_rows - 1}});
		}
		swapScreen();
		cursorControl(TCursor::Control::LOAD);
	}
	m_cursor = saved_cursor;
}

void Term::clearRegion(Range range) {
	if (range.begin.x > range.end.x)
		std::swap(range.begin.x, range.end.x);
	if (range.begin.y > range.end.y)
		std::swap(range.begin.y, range.end.y);

	range.clamp(m_cols-1, m_rows-1);

	for (auto y = range.begin.y; y <= range.end.y; y++) {
		m_dirty_lines[y] = true;
		for (auto x = range.begin.x; x <= range.end.x; x++) {
			auto &gp = m_screen[y][x];
			if (m_selection.isSelected(x, y))
				m_selection.clear();
			gp.fg = m_cursor.attr.fg;
			gp.bg = m_cursor.attr.bg;
			gp.mode.reset();
			gp.u = ' ';
		}
	}
}

void Term::setScroll(int top, int bottom) {
	clampRow(top);
	clampRow(bottom);

	if (top > bottom) {
		std::swap(top, bottom);
	}

	m_top_scroll = top;
	m_bottom_scroll = bottom;
}

void Term::moveCursorTo(CharPos pos) {
	int miny, maxy;

	if (m_cursor.state[TCursor::State::ORIGIN]) {
		miny = m_top_scroll;
		maxy = m_bottom_scroll;
	} else {
		miny = 0;
		maxy = m_rows - 1;
	}

	m_cursor.state.reset(TCursor::State::WRAPNEXT);
	clampCol(pos.x);
	pos.clampY(miny, maxy);

	m_cursor.pos = pos;
}

/* for absolute user moves, when decom is set */
void Term::moveCursorAbsTo(CharPos pos) {
	if (m_cursor.state[TCursor::State::ORIGIN])
		pos.y += m_top_scroll;
	moveCursorTo(pos);
}

void Term::swapScreen() {
	std::swap(m_screen, m_alt_screen);
	m_mode.flip(Mode::ALTSCREEN);
	setAllDirty();
}

void Term::cursorControl(const TCursor::Control &ctrl)
{
	auto &cursor = m_mode[Mode::ALTSCREEN] ? m_cached_cursors[1] : m_cached_cursors[0];

	if (ctrl == TCursor::Control::SAVE) {
		cursor = m_cursor;
	} else if (ctrl == TCursor::Control::LOAD) {
		m_cursor = cursor;
		moveCursorTo(cursor.pos);
	}
}

int Term::getLineLen(int y) const
{
	auto col = m_cols;
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
		while (x < m_cols && n--)
			for (++x; x < m_cols && !m_tabs[x]; ++x)
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

	if (new_pos.y == m_bottom_scroll) {
		scrollUp(m_top_scroll, 1);
	} else {
		new_pos.y++;
	}

	moveCursorTo(new_pos);
}

void Term::deleteChar(int n) {
	n = std::clamp(n, 0, m_cols - m_cursor.pos.x);

	const int dst = m_cursor.pos.x;
	const int src = m_cursor.pos.x + n;
	const int size = m_cols - src;
	auto &line = m_screen[m_cursor.pos.y];

	// slide remaining line content n characters to the left
	std::memmove(&line[dst], &line[src], size * sizeof(Glyph));
	// clear n characters at end of line
	clearRegion({CharPos{m_cols-n, m_cursor.pos.y}, CharPos{m_cols-1, m_cursor.pos.y}});
}

void Term::deleteLine(int n) {
	if (in_range(m_cursor.pos.y, m_top_scroll, m_bottom_scroll))
		scrollUp(m_cursor.pos.y, n);
}

void Term::insertBlank(int n) {
	n = std::clamp(n, 0, m_cols - m_cursor.pos.x);

	const int dst = m_cursor.pos.x + n;
	const int src = m_cursor.pos.x;
	const int size = m_cols - dst;
	auto &line = m_screen[m_cursor.pos.y];

	std::memmove(&line[dst], &line[src], size * sizeof(Glyph));
	clearRegion({CharPos{src, m_cursor.pos.y}, CharPos{dst - 1, m_cursor.pos.y}});
}

void Term::insertBlankLine(int n)
{
	if (in_range(m_cursor.pos.y, m_top_scroll, m_bottom_scroll))
		scrollDown(m_cursor.pos.y, n);
}

void Term::scrollDown(int orig, int n)
{
	n = std::clamp(n, 0, m_bottom_scroll-orig+1);

	setDirty(orig, m_bottom_scroll-n);
	clearRegion({CharPos{0, m_bottom_scroll-n+1}, CharPos{m_cols-1, m_bottom_scroll}});

	for (int i = m_bottom_scroll; i >= orig+n; i--) {
		std::swap(m_screen[i], m_screen[i-n]);
	}

	m_selection.scroll(orig, n);
}

void Term::scrollUp(int orig, int n)
{
	n = std::clamp(n, 0, m_bottom_scroll-orig+1);

	setDirty(orig+n, m_bottom_scroll);
	clearRegion({CharPos{0, orig}, CharPos{m_cols-1, orig+n-1}});

	for (int i = orig; i <= m_bottom_scroll-n; i++) {
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
		const uint r = attr[npar + 2];
		const uint g = attr[npar + 3];
		const uint b = attr[npar + 4];
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
				xsetmode(set, WinMode::APPCURSOR);
				break;
			case 5: /* DECSCNM -- Reverse video */
				xsetmode(set, WinMode::REVERSE);
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
				xsetmode(!set, WinMode::HIDE);
				break;
			case 9:    /* X10 mouse compatibility mode */
				xsetpointermotion(0);
				xsetmode(false, WinMode::MOUSE);
				xsetmode(set, WinMode::MOUSEX10);
				break;
			case 1000: /* 1000: report button press */
				xsetpointermotion(0);
				xsetmode(false, WinMode::MOUSE);
				xsetmode(set, WinMode::MOUSEBTN);
				break;
			case 1002: /* 1002: report motion on button press */
				xsetpointermotion(0);
				xsetmode(false, WinMode::MOUSE);
				xsetmode(set, WinMode::MOUSEMOTION);
				break;
			case 1003: /* 1003: enable all mouse motions */
				xsetpointermotion(set);
				xsetmode(false, WinMode::MOUSE);
				xsetmode(set, WinMode::MOUSEMANY);
				break;
			case 1004: /* 1004: send focus events to tty */
				xsetmode(set, WinMode::FOCUS);
				break;
			case 1006: /* 1006: extended reporting mode */
				xsetmode(set, WinMode::MOUSESGR);
				break;
			case 1034:
				xsetmode(set, WinMode::EIGHT_BIT);
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
				xsetmode(set, WinMode::BRCKTPASTE);
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
				xsetmode(set, WinMode::KBDLOCK);
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
	auto end = bp + std::min(getLineLen(n), m_cols) - 1;
	if (bp != end || bp->u != ' ') {
		for ( ; bp <= end; ++bp)
			m_tty.printToIoFile(buf, utf8::encode(bp->u, buf));
	}
	m_tty.printToIoFile("\n", 1);
}

bool Term::testAttrSet(const Glyph::Attr &attr) const {
	for (int y = 0; y < m_rows-1; y++) {
		for (int x = 0; x < m_cols-1; x++) {
			if (m_screen[y][x].mode[attr])
				return true;
		}
	}

	return false;
}

void Term::setDirtyByAttr(const Glyph::Attr &attr) {
	for (int y = 0; y < m_rows-1; y++) {
		for (int x = 0; x < m_cols-1; x++) {
			if (m_screen[y][x].mode[attr]) {
				setDirty(y, y);
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
		xdrawline(m_screen[y], range.begin.x, y, range.end.x);
	}
}

void Term::draw() {
	if (!xstartdraw())
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
	xdrawcursor(
		old_cx, m_cursor.pos.y,
		m_screen[m_cursor.pos.y][old_cx],
		m_old_cursor_pos.x, m_old_cursor_pos.y,
		getGlyphAt(m_old_cursor_pos)
	);

	m_old_cursor_pos.set(old_cx, m_cursor.pos.y);
	xfinishdraw();
	if (m_old_cursor_pos != old_cursor_pos)
		xximspot(m_old_cursor_pos);
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
		if (pos.x+1 < m_cols) {
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
		for (int x = 0; x < m_cols; ++x) {
			for (int y = 0; y < m_rows; ++y)
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
			xbell();
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

	if (m_selection.isSelected(m_cursor.pos.x, m_cursor.pos.y))
		m_selection.clear();

	Glyph *gp = &getGlyphAt(m_cursor.pos);
	if (m_mode[Mode::WRAP] && m_cursor.state[TCursor::State::WRAPNEXT]) {
		gp->mode.set(Attr::WRAP);
		putNewline();
		gp = &getGlyphAt(m_cursor.pos);
	}

	if (m_mode[Mode::INSERT] && m_cursor.pos.x + width < m_cols)
		std::memmove(gp+width, gp, (m_cols - m_cursor.pos.x - width) * sizeof(Glyph));

	if (m_cursor.pos.x + width > m_cols) {
		putNewline();
		gp = &getGlyphAt(m_cursor.pos);
	}

	setChar(u, m_cursor.attr, m_cursor.pos);
	m_last_char = u;

	if (width == 2) {
		gp->mode.set(Attr::WIDE);
		if (m_cursor.pos.x + 1 < m_cols) {
			if (gp[1].mode[Attr::WIDE] && m_cursor.pos.x + 2 < m_cols) {
				gp[2].u = ' ';
				gp[2].mode.reset(Attr::WDUMMY);
			}
			gp[1].u = '\0';
			gp[1].mode.limit(Attr::WDUMMY);
		}
	}

	if (m_cursor.pos.x + width < m_cols) {
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
