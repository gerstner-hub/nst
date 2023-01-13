// C
#include <string.h>

// stdlib
#include <algorithm>
#include <cstring>
#include <iostream>
#include <type_traits>

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

// we're relying on Glyph being POD so that we can memmove individual Glyphs
// in this unit
static_assert(std::is_pod<Glyph>::value, "Glyph type needs to be POD because of memmove");

} // end anon ns

namespace nst {

typedef Glyph::Attr Attr;

Term::TCursor::TCursor() {
	m_attr.fg = config::DEFAULTFG;
	m_attr.bg = config::DEFAULTBG;
}

bool Term::TCursor::setAttrs(const std::vector<int> &attrs) {
	bool ret = true;
	auto &mode = m_attr.mode;

	for (size_t i = 0; i < attrs.size(); i++) {
		const auto &attr = attrs[i];
		switch (attr) {
		case 0:
			m_attr.mode.reset({
				Attr::BOLD,
				Attr::FAINT,
				Attr::ITALIC,
				Attr::UNDERLINE,
				Attr::BLINK,
				Attr::REVERSE,
				Attr::INVISIBLE,
				Attr::STRUCK
			});
			m_attr.fg = config::DEFAULTFG;
			m_attr.bg = config::DEFAULTBG;
			break;
		case 1:
			mode.set(Attr::BOLD);
			break;
		case 2:
			mode.set(Attr::FAINT);
			break;
		case 3:
			mode.set(Attr::ITALIC);
			break;
		case 4:
			mode.set(Attr::UNDERLINE);
			break;
		case 5: /* slow blink */
			/* FALLTHROUGH */
		case 6: /* rapid blink */
			mode.set(Attr::BLINK);
			break;
		case 7:
			mode.set(Attr::REVERSE);
			break;
		case 8:
			mode.set(Attr::INVISIBLE);
			break;
		case 9:
			mode.set(Attr::STRUCK);
			break;
		case 22:
			mode.reset({Attr::BOLD, Attr::FAINT});
			break;
		case 23:
			mode.reset(Attr::ITALIC);
			break;
		case 24:
			mode.reset(Attr::UNDERLINE);
			break;
		case 25:
			mode.reset(Attr::BLINK);
			break;
		case 27:
			mode.reset(Attr::REVERSE);
			break;
		case 28:
			mode.reset(Attr::INVISIBLE);
			break;
		case 29:
			mode.reset(Attr::STRUCK);
			break;
		case 38: {
			int32_t colidx;
			if (auto parsed = parseColor(attrs, i + 1, colidx); parsed > 0) {
				m_attr.fg = colidx;
				i += parsed;
			}
			break;
		}
		case 39:
			m_attr.fg = config::DEFAULTFG;
			break;
		case 48: {
			int32_t colidx;
			if (auto parsed = parseColor(attrs, i + 1, colidx); parsed > 0) {
				m_attr.bg = colidx;
				i += parsed;
			}
			break;
		}
		case 49:
			m_attr.bg = config::DEFAULTBG;
			break;
		default:
			if (in_range(attr, 30, 37)) {
				m_attr.fg = attr - 30;
			} else if (in_range(attr, 40, 47)) {
				m_attr.bg = attr - 40;
			} else if (in_range(attr, 90, 97)) {
				m_attr.fg = attr - 90 + 8;
			} else if (in_range(attr, 100, 107)) {
				m_attr.bg = attr - 100 + 8;
			} else {
				std::cerr << "erresc(default): gfx attr " << attr << " unknown\n",
				ret = false;
			}
			break;
		}
	}

	return ret;
}

size_t Term::TCursor::parseColor(const std::vector<int> &attrs, size_t idx, int32_t &colidx) {

	auto badPars = [&]() {
		std::cerr << "erresc(38): Incorrect number of parameters (" << attrs.size() << ")\n";
		return 0;
	};

	if (attrs.size() == idx)
		return badPars();

	const auto coltype = attrs[idx++];
	const auto left = attrs.size() - idx;

	switch (coltype) {
	case 2: /* direct color in RGB space */ {
		if (left < 3)
			return badPars();

		const auto [r, g, b] = std::tie(attrs[idx], attrs[idx+1], attrs[idx+2]);

		if (r <= 255 && g <= 255 && b <= 255) {
			colidx = toTrueColor(r, g, b);
		} else {
			std::cerr << "erresc: bad rgb color (" << r << "," << g << "," << b << ")" << std::endl;
		}

		return 4;
	}
	case 5: /* indexed color */ {
		if (left < 1)
			return badPars();

		const auto col = attrs[idx];

		if (in_range(col, 0, 255)) {
			colidx = col;
		} else {
			std::cerr << "erresc: bad fg/bgcolor " << col << std::endl;
		}

		return 2;
	}
	case 0: /* implementation defined (only foreground) */
	case 1: /* transparent */
	case 3: /* direct color in CMY space */
	case 4: /* direct color in CMYK space */
	default:
		std::cerr << "erresc(38): gfx attr " << coltype << " unknown" << std::endl;
		return 0;
	}
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
	resetScrollArea();
	m_mode.set({Mode::WRAP, Mode::UTF8});
	m_charsets.fill(Charset::USA);
	m_active_charset = 0;

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
	resetScrollArea();
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
			gp.clear(m_cursor.getAttr());
		}
	}
}

void Term::clearRegion(const LineSpan &span) {
	clearRegion({CharPos{0, span.top}, CharPos{m_size.cols - 1, span.bottom}});
}

void Term::setScrollArea(const LineSpan &span) {
	m_scroll_area = span;
	clamp(m_scroll_area);
	m_scroll_area.sanitize();
}

void Term::moveCursorTo(CharPos pos) {
	LineSpan limit{0, m_size.rows - 1};

	if (m_cursor.state[TCursor::State::ORIGIN]) {
		limit = m_scroll_area;
	}

	m_cursor.state.reset(TCursor::State::WRAPNEXT);
	clampCol(pos.x);
	pos.clampY(limit.top, limit.bottom);
	m_cursor.pos = pos;
}

/* for absolute user moves, when decom is set */
void Term::moveCursorAbsTo(CharPos pos) {
	if (m_cursor.state[TCursor::State::ORIGIN])
		pos.y += m_scroll_area.top;
	moveCursorTo(pos);
}

void Term::swapScreen() {
	std::swap(m_screen, m_alt_screen);
	m_mode.flip(Mode::ALTSCREEN);
	setAllDirty();
}

void Term::cursorControl(const TCursor::Control &ctrl) {
	auto &cursor = m_mode[Mode::ALTSCREEN] ? m_cached_alt_cursor : m_cached_main_cursor;

	switch(ctrl) {
		case TCursor::Control::SAVE:
			cursor = m_cursor;
			break;
		case TCursor::Control::LOAD:
			m_cursor = cursor;
			moveCursorTo(cursor.pos);
			break;
	}
}

int Term::getLineLen(const CharPos &pos) const {
	const auto &line = m_screen[pos.y];

	if (line.back().mode[Attr::WRAP])
		return m_size.cols;

	auto last_col = std::find_if(
			line.rbegin(), line.rend(),
			[](const Glyph &g) { return g.hasValue(); });

	return line.rend() - last_col;
}

void Term::moveToNextTab(size_t count) {
	auto x = m_cursor.pos.x;

	while (count && x < m_size.cols) {
		for (++x; x < m_size.cols && !m_tabs[x]; ++x)
			; // noop
		count--;
	}

	m_cursor.pos.x = limitCol(x);
}

void Term::moveToPrevTab(size_t count) {
	auto x = m_cursor.pos.x;

	while (count && x > 0) {
		for (--x; x > 0 && !m_tabs[x]; --x)
			; // noop
		count--;
	}

	m_cursor.pos.x = limitCol(x);
}

void Term::moveToNewline(const CarriageReturn cr) {
	CharPos new_pos = m_cursor.pos;

	if (cr)
		new_pos.x = 0;

	if (new_pos.y == m_scroll_area.bottom) {
		scrollUp();
	} else {
		new_pos.y++;
	}

	moveCursorTo(new_pos);
}

void Term::deleteColsAfterCursor(int count) {
	count = std::clamp(count, 0, colsLeft());

	const auto &cursor = m_cursor.pos;
	const int dst = cursor.x;
	const int src = cursor.x + count;
	const int left = m_size.cols - src;
	auto &line = getLine(cursor);

	// slide remaining line content count characters to the left
	std::memmove(&line[dst], &line[src], left * sizeof(Glyph));
	// clear count characters at end of line
	clearRegion(Range{
		CharPos{m_size.cols - count, cursor.y},
		Range::Width{count}
	});
}

void Term::deleteLinesBelowCursor(int count) {
	if (m_scroll_area.inRange(m_cursor.pos)) {
		scrollUp(count, m_cursor.pos.y);
	}
}

void Term::insertBlanksAfterCursor(int count) {
	count = std::clamp(count, 0, colsLeft());

	const auto &cursor = m_cursor.pos;
	const int dst = cursor.x + count;
	const int src = cursor.x;
	const int left = m_size.cols - dst;
	auto &line = getLine(cursor);

	std::memmove(&line[dst], &line[src], left * sizeof(Glyph));
	clearRegion(Range{
		CharPos{src, cursor.y},
		Range::Width{count}
	});
}

void Term::insertBlankLinesBelowCursor(int count) {
	if (m_scroll_area.inRange(m_cursor.pos)) {
		scrollDown(count, m_cursor.pos.y);
	}
}

void Term::scrollDown(int num_lines, std::optional<int> opt_origin) {
	const auto &area = m_scroll_area;
	int origin = opt_origin ? *opt_origin : m_scroll_area.top;

	num_lines = std::clamp(num_lines, 0, area.bottom - origin + 1);

	setDirty(LineSpan{origin, area.bottom - num_lines});
	clearRegion(LineSpan{area.bottom - num_lines + 1, area.bottom});

	for (int i = area.bottom; i >= origin + num_lines; i--) {
		std::swap(m_screen[i], m_screen[i-num_lines]);
	}

	m_selection.scroll(origin, num_lines);
}

void Term::scrollUp(int num_lines, std::optional<int> opt_origin) {
	const auto &area = m_scroll_area;
	int origin = opt_origin ? *opt_origin : m_scroll_area.top;

	num_lines = std::clamp(num_lines, 0, area.bottom - origin + 1);

	setDirty(LineSpan{origin + num_lines, area.bottom});
	clearRegion(LineSpan{origin, origin + num_lines -1});

	for (int i = origin; i <= area.bottom - num_lines; i++) {
		std::swap(m_screen[i], m_screen[i+num_lines]);
	}

	m_selection.scroll(origin, -num_lines);
}

void Term::setMode(bool priv, const bool set, const std::vector<int> &args) {
	if (priv) {
		setPrivateMode(set, args);
		return;
	}

	for (const auto arg: args) {
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

void Term::setPrivateMode(const bool set, const std::vector<int> &args) {
	for (const auto arg: args) {
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
	}
}

void Term::dumpLine(const CharPos &pos) const {
	char buf[utf8::UTF_SIZE];

	auto left = getLineLen(pos);
	const auto line = getLine(pos);

	for (auto it = line.begin(); left != 0; it++, left--) {
		m_tty.printToIoFile(buf, utf8::encode(it->u, buf));
	}
	m_tty.printToIoFile("\n");
}

bool Term::existsBlinkingGlyph() const {
	// NOTE: this test could probably be cheaper by keeping track of this
	// attribute when changing glyphs.

	for (auto &line: m_screen) {
		for (auto &glyph: line) {
			if (glyph.mode[Attr::BLINK]) {
				return true;
			}
		}
	}

	return false;
}

void Term::setDirtyByAttr(const Glyph::Attr &attr) {
	for (int y = 0; y < m_size.rows - 1; y++) {
		const auto &line = m_screen[y];

		for (const auto &glyph: line) {
			if (glyph.mode[attr]) {
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
		m_x11.drawLine(m_screen[y], CharPos{range.begin.x, y}, cosmos::to_integral(range.width()));
	}
}

void Term::draw() {
	if (!m_x11.canDraw())
		return;

	const auto orig_last_pos = m_last_cursor_pos;
	auto new_pos = m_cursor.pos;

	/* make sure the last cursor pos is still sane */
	clampToScreen(m_last_cursor_pos);

	// in case we point to a wide character dummy position, move one
	// character to the left to point to the actual character
	if (getGlyphAt(m_last_cursor_pos).isDummy())
		m_last_cursor_pos.moveLeft();
	if (getGlyphAt(new_pos).isDummy())
		new_pos.moveLeft();

	drawScreen();
	m_x11.clearCursor(m_last_cursor_pos, getGlyphAt(m_last_cursor_pos));
	m_x11.drawCursor(new_pos, getGlyphAt(new_pos));

	const bool cursor_pos_changed = orig_last_pos != new_pos;
	m_last_cursor_pos = new_pos;
	m_x11.finishDraw();

	if (cursor_pos_changed)
		m_x11.getInput().setSpot(new_pos);
}

void Term::initStrSequence(unsigned char ch) {
	// translate if not already done
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

Rune Term::translateChar(Rune u) {
	// GRAPHIC0 translation table for VT100 "special graphics mode"
	/*
	 * The table is proudly stolen from rxvt.
	 */

	constexpr auto VT100_GR_START = 0x41;
	constexpr auto VT100_GR_END = 0x7e;

	constexpr const char *VT100_0[VT100_GR_END - VT100_GR_START + 1] = { /* 0x41 - 0x7e */
		"↑", "↓", "→", "←", "█", "▚", "☃", /* A - G */
		0, 0, 0, 0, 0, 0, 0, 0, /* H - O */
		0, 0, 0, 0, 0, 0, 0, 0, /* P - W */
		0, 0, 0, 0, 0, 0, 0, " ", /* X - _ */
		"◆", "▒", "␉", "␌", "␍", "␊", "°", "±", /* ` - g */
		"␤", "␋", "┘", "┐", "┌", "└", "┼", "⎺", /* h - o */
		"⎻", "─", "⎼", "⎽", "├", "┤", "┴", "┬", /* p - w */
		"│", "≤", "≥", "π", "≠", "£", "·", /* x - ~ */
	};

	switch (m_charsets[m_active_charset]) {
		// nothing to do or not implemented
		default: break;
		case Charset::GRAPHIC0:
			 if (in_range(u, VT100_GR_START, VT100_GR_END)) {
				 const auto TRANS_CHAR = VT100_0[u - VT100_GR_START];

				 if (TRANS_CHAR) {
					utf8::decode(TRANS_CHAR, &u, utf8::UTF_SIZE);
				 }
			 }
			 break;
	}

	return u;
}

void Term::setChar(Rune u, const CharPos &pos) {
	auto &glyph = getGlyphAt(pos);

	// if we replace a WIDE/DUMMY position then correct the sibbling
	// position
	if (glyph.mode[Attr::WIDE]) {
		if (!atEndOfLine(pos)) {
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
	glyph = m_cursor.getAttr();
	glyph.u = translateChar(u);
}

void Term::setCharsetMapping(const char code) {
	// this is DEC VT100 spec related
	switch(code) {
	default:
		std::cerr << "esc unhandled charset: ESC ( " << code << "\n";
		break;
	case '0':
		m_charsets[m_esc_charset] = Charset::GRAPHIC0;
		break;
	case 'B':
		m_charsets[m_esc_charset] = Charset::USA;
		break;
	}
}

void Term::runDECTest(char code) {
	if (code == '8') { /* DEC screen alignment test. */
		for (int x = 0; x < m_size.cols; ++x) {
			for (int y = 0; y < m_size.rows; ++y)
				setChar('E', CharPos{x, y});
		}
	}
}

void Term::handleControlCode(unsigned char ascii) {
	switch (ascii) {
	case '\t':   /* HT */
		moveToNextTab();
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
		moveToNewline(CarriageReturn(m_mode[Mode::CRLF]));
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
		m_active_charset = 1 - (ascii - '\016');
		return;
	case '\032': /* SUB */
		setChar('?', m_cursor.pos);
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
		moveToNewline(); /* always go to first col */
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
		initStrSequence(ascii);
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
			setCharsetMapping(u);
		} else if (m_esc_state[Escape::TEST]) {
			runDECTest(u);
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
		moveToNewline();
		gp = &getGlyphAt(m_cursor.pos);
	}

	if (m_mode[Mode::INSERT] && m_cursor.pos.x + width < m_size.cols)
		std::memmove(gp+width, gp, (m_size.cols - m_cursor.pos.x - width) * sizeof(Glyph));

	if (m_cursor.pos.x + width > m_size.cols) {
		moveToNewline();
		gp = &getGlyphAt(m_cursor.pos);
	}

	setChar(u, m_cursor.pos);
	m_last_char = u;

	if (width == 2) {
		gp->mode.set(Attr::WIDE);
		if (m_cursor.pos.x + 1 < m_size.cols) {
			if (gp[1].mode[Attr::WIDE] && m_cursor.pos.x + 2 < m_size.cols) {
				gp[2].u = ' ';
				gp[2].mode.reset(Attr::WDUMMY);
			}
			gp[1].u = '\0';
			gp[1].mode = Glyph::AttrBitMask(Attr::WDUMMY);
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
