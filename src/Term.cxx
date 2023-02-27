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

namespace {

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

void Term::TCursor::resetAttrs() {
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
}

Term::Term(Nst &nst) :
	m_selection(nst.getSelection()),
	m_tty(nst.getTTY()),
	m_x11(nst.getX11()),
	m_allowaltscreen(config::ALLOWALTSCREEN),
	m_esc_handler(nst)
{}

void Term::init(const Nst &nst) {

	if (auto &cmdline = nst.getCmdline(); cmdline.use_alt_screen.isSet()) {
		m_allowaltscreen = cmdline.use_alt_screen.getValue();
	}

	resize(m_x11.getTermSize());
	reset();
}

void Term::reset() {
	m_cursor = TCursor();

	clearAllTabs();
	for (auto i = config::TABSPACES; i < m_size.cols; i += config::TABSPACES)
		m_tabs[i] = true;
	resetScrollArea();
	// TODO: A test with disabled WRAP mode showed that the screen kind of
	// scrolls right and back left again (when deleting characters) but
	// the original screen content does not appear again.
	m_mode.set({Mode::WRAP, Mode::UTF8});
	m_charsets.fill(Charset::USA);
	m_active_charset = 0;

	// reset main and alt screen
	for (size_t i = 0; i < 2; i++) {
		moveCursorTo(topLeft());
		cursorControl(TCursor::Control::SAVE);
		clearScreen();
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
		screen->setDimension(new_size);
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
			auto &gp = m_screen[pos];
			gp.clear(m_cursor.getAttr());
		}
	}
}

void Term::clearRegion(const LineSpan &span) {
	clearRegion({CharPos{0, span.top}, CharPos{m_size.cols - 1, span.bottom}});
}

void Term::clearLinesBelowCursor() {
	if (isCursorAtBottom())
		// nothing to do
		return;

	const auto &curpos = getCursor().getPos();
	clearRegion(Range{curpos.nextLine().startOfLine(), bottomRight()});
}

void Term::clearLinesAboveCursor() {
	if (isCursorAtTop())
		// nothing to do
		return;

	const auto &curpos = getCursor().getPos();
	clearRegion(Range{topLeft(), atEndOfLine(curpos.prevLine())});
}

void Term::clearCursorLine() {
	const auto &curpos = getCursor().getPos();
	clearRegion(LineSpan{curpos.y, curpos.y});
}

void Term::clearColsBeforeCursor() {
	const auto &curpos = getCursor().getPos();
	clearRegion({curpos.startOfLine(), curpos});
}

void Term::clearColsAfterCursor() {
	const auto &curpos = getCursor().getPos();
	clearRegion(Range{curpos, atEndOfLine(curpos)});
}

bool Term::isCursorAtBottom() const {
	return getCursor().getPos().y == m_size.rows - 1;
}

bool Term::isCursorAtTop() const {
	return getCursor().getPos().y == 0;
}

void Term::setScrollArea(const LineSpan &span) {
	m_scroll_area = span;
	clamp(m_scroll_area);
	m_scroll_area.sanitize();
}

void Term::moveCursorTo(CharPos pos) {
	LineSpan limit{0, m_size.rows - 1};

	if (m_cursor.useOrigin()) {
		limit = m_scroll_area;
	}

	m_cursor.setWrapNext(false);
	clampCol(pos.x);
	pos.clampY(limit.top, limit.bottom);
	m_cursor.pos = pos;
}

void Term::moveCursorAbsTo(CharPos pos) {
	if (m_cursor.useOrigin())
		pos.y += m_scroll_area.top;
	moveCursorTo(pos);
}

void Term::setAltScreen(const bool enable, const bool with_cursor) {
	if (!m_allowaltscreen)
		return;

	const auto cursor_ctrl = enable ? TCursor::Control::SAVE : TCursor::Control::LOAD;

	if (with_cursor)
		cursorControl(cursor_ctrl);

	const auto is_alt = m_mode[Mode::ALTSCREEN];

	if (is_alt) {
		clearRegion({topLeft(), bottomRight()});
	}

	if (enable ^ is_alt) // if the mode actually changed
		swapScreen();

	if (with_cursor)
		cursorControl(cursor_ctrl);
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
	auto &line = m_screen.line(cursor);

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
	auto &line = m_screen.line(cursor);

	// slide remaining line content count characters to the right
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

void Term::doLineFeed() {
	const auto &curpos = getCursor().getPos();

	if (curpos.y == getScrollArea().bottom) {
		scrollUp(1);
	} else {
		moveCursorTo(curpos.nextLine());
	}
}

void Term::doReverseLineFeed() {
	const auto &curpos = getCursor().getPos();

	if (curpos.y == getScrollArea().top) {
		scrollDown(1);
	} else {
		moveCursorTo(curpos.prevLine());
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

void Term::dumpLine(const CharPos &pos) const {
	char buf[utf8::UTF_SIZE];

	auto left = getLineLen(pos);
	const auto line = m_screen.line(pos);

	for (auto it = line.begin(); left != 0; it++, left--) {
		auto len = utf8::encode(it->u, buf);
		m_tty.printToIoFile({buf, len});
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
	if (m_screen[m_last_cursor_pos].isDummy())
		m_last_cursor_pos.moveLeft();
	if (m_screen[new_pos].isDummy())
		new_pos.moveLeft();

	drawScreen();
	m_x11.clearCursor(m_last_cursor_pos, m_screen[m_last_cursor_pos]);
	m_x11.drawCursor(new_pos, m_screen[new_pos]);

	const bool cursor_pos_changed = orig_last_pos != new_pos;
	m_last_cursor_pos = new_pos;
	m_x11.finishDraw();

	if (cursor_pos_changed)
		m_x11.getInput().setSpot(new_pos);
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
			 if (cosmos::in_range(u, VT100_GR_START, VT100_GR_END)) {
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
	auto &glyph = m_screen[pos];

	// if we replace a WIDE/DUMMY position then correct the sibbling
	// position
	if (glyph.mode[Attr::WIDE]) {
		if (!isAtEndOfLine(pos)) {
			auto &next_glyph = m_screen[pos.nextCol()];
			next_glyph.u = ' ';
			next_glyph.mode.reset(Attr::WDUMMY);
		}
	} else if (glyph.mode[Attr::WDUMMY]) {
		auto &prev_glyph = m_screen[pos.prevCol()];
		prev_glyph.u = ' ';
		prev_glyph.mode.reset(Attr::WIDE);
	}

	m_dirty_lines[pos.y] = true;
	glyph = m_cursor.getAttr();
	glyph.u = translateChar(u);
}

void Term::runDECTest() {
	/* DEC screen alignment test: fill screen with E's */
	for (int x = 0; x < m_size.cols; ++x) {
		for (int y = 0; y < m_size.rows; ++y)
			setChar('E', CharPos{x, y});
	}
}

void Term::repeatChar(int count) {
	if (m_last_char == 0)
		// nothing to repeat
		return;

	while (count-- > 0)
		putChar(m_last_char);
}

void Term::putChar(Rune rune) {
	const auto rinfo = RuneInfo(rune, m_mode[Term::Mode::UTF8]);

	if (isPrintMode()) {
		m_tty.printToIoFile(rinfo.getEncoded());
	}

	if (m_esc_handler.process(rinfo) == EscapeHandler::WasProcessed(true))
		// input was part of a special control sequence
		return;

	if (m_selection.isSelected(m_cursor.pos))
		m_selection.clear();

	Glyph *gp = getCurGlyph();

	// perform automatic line wrap, if necessary
	if (m_mode[Mode::WRAP] && m_cursor.needWrapNext()) {
		gp->mode.set(Attr::WRAP);
		moveToNewline();
		gp = getCurGlyph();
	}

	const auto req_width = rinfo.width();

	// shift any remaining Glyphs to the right
	if (m_mode[Mode::INSERT]) {
		if (auto to_move = lineSpaceLeft() - req_width; to_move > 0) {
			std::memmove(gp + req_width, gp, to_move * sizeof(Glyph));
		}
	}

	if (lineSpaceLeft() < req_width) {
		moveToNewline();
		gp = getCurGlyph();
	}

	setChar(rune, m_cursor.pos);
	m_last_char = rune;
	const auto left_chars = lineSpaceLeft();

	if (rinfo.isWide()) {
		gp->mode.set(Attr::WIDE);

		if (left_chars > 1) {
			// mark the follow-up position as dummy
			auto &next = gp[1];

			// if we are overriding another wide character, clean
			// up the dummy follow-up
			if (next.isWide() && left_chars > 2) {
				auto &after_next = gp[2];
				after_next.u = ' ';
				after_next.mode.reset(Attr::WDUMMY);
			}

			next.u = '\0';
			next.mode = Glyph::AttrBitMask(Attr::WDUMMY);
		}
	}

	if (left_chars > req_width) {
		moveCursorTo(m_cursor.pos.nextCol(req_width));
	} else {
		m_cursor.setWrapNext(true);
	}
}

size_t Term::write(const std::string_view &data, const ShowCtrlChars &show_ctrl) {
	Rune u;
	size_t charsize = 0;
	const bool use_utf8 = m_mode[Mode::UTF8];

	for (size_t pos = 0; pos < data.size(); pos += charsize) {
		if (use_utf8) {
			/* process a complete utf8 char */
			charsize = utf8::decode(data.data() + pos, &u, data.size() - pos);
			if (charsize == 0)
				return pos;
		} else {
			u = data[pos] & 0xFF;
			charsize = 1;
		}

		if (show_ctrl && RuneInfo::isControlChar(u)) {
			// add symbolic annotation for control chars
			if (u & 0x80) {
				u &= 0x7f;
				putChar('^');
				putChar('[');
			} else if (!cosmos::in_list(static_cast<char>(u), {'\n', '\r', '\t'})) {
				u ^= 0x40;
				putChar('^');
			}
		}

		putChar(u);
	}

	return data.size();
}

} // end ns
