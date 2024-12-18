// C++
#include <cassert>
#include <cstring>
#include <type_traits>

// cosmos
#include "cosmos/utils.hxx"

// nst
#include "codecs.hxx"
#include "nst_config.hxx"
#include "nst.hxx"
#include "Selection.hxx"
#include "Term.hxx"
#include "TTY.hxx"

namespace nst {

// we're relying on Glyph being POD so that we can memmove individual Glyphs
// in this unit
static_assert(std::is_trivially_copyable<nst::Glyph>::value, "Glyph type needs to be POD because of memmove");

ColorIndex CursorState::m_def_fg = ColorIndex::INVALID;
ColorIndex CursorState::m_def_bg = ColorIndex::INVALID;

CursorState::CursorState() {
	m_attrs.rune = ' ';
	m_attrs.fg = m_def_fg;
	m_attrs.bg = m_def_bg;
}

void CursorState::resetAttrs() {
	m_attrs.mode.reset({
		Attr::BOLD,
		Attr::FAINT,
		Attr::ITALIC,
		Attr::UNDERLINE,
		Attr::BLINK,
		Attr::REVERSE,
		Attr::INVISIBLE,
		Attr::STRUCK
	});
	m_attrs.fg = m_def_fg;
	m_attrs.bg = m_def_bg;
	m_attrs.rune = ' ';
}

Term::Term(Nst &nst) :
		m_selection{nst.selection()},
		m_tty{nst.tty()},
		m_wsys{nst.wsys()},
		m_allow_altscreen{config::ALLOW_ALTSCREEN},
		m_esc_handler{nst},
		m_screen{config::HISTORY_LEN},
		m_saved_screen{0, /*is_alt_screen=*/true},
		m_keep_scroll_position{config::KEEP_SCROLL_POSITION}
{}

void Term::init(const Nst &nst) {

	if (auto &cmdline = nst.cmdline(); cmdline.use_alt_screen.isSet()) {
		m_allow_altscreen = cmdline.use_alt_screen.getValue();
	}

	const auto theme = nst.theme();
	m_cursor.setDefaultColors(theme.fg, theme.bg);

	auto &config_file = nst.configFile();

	if (auto keep_scroll = config_file.asBool("keep_scroll_position"); keep_scroll != std::nullopt) {
		m_keep_scroll_position = *keep_scroll;
	}

	if (auto history_len = config_file.asUnsigned("history_len"); history_len != std::nullopt) {
		m_screen.setHistoryLen(*history_len);
	}

	resize(m_wsys.termWin().getTermDim());
	reset();
}

void Term::themeChanged(const Theme &old_theme, const Theme &new_theme) {
	const auto new_max_color = ColorIndex{
		cosmos::to_integral(ColorIndex::END_256) + static_cast<std::underlying_type<ColorIndex>::type>(new_theme.extended_colors.size())
	};

	/* since the new theme might have fewer extended colors than the old
	 * one we need to adjust Glyph colors throughout the screen buffer.
	 *
	 * also default colors need to be replaced
	 *
	 * if default colors in the old theme match one of the 256 standard
	 * colors then we won't be able to reliably replace them (it is not
	 * possible to keep apart default color from the actual color).
	 */

	auto fixupFgColor = [new_theme, old_theme, new_max_color](const ColorIndex fg) {
		if (fg > ColorIndex::END_256 && fg == old_theme.fg)
			return new_theme.fg;
		else if (fg > ColorIndex::END_256 && fg > new_max_color)
			return new_theme.fg;

		return fg;
	};
	auto fixupBgColor = [new_theme, old_theme, new_max_color](const ColorIndex bg) {
		if (bg > ColorIndex::END_256 && bg == old_theme.bg)
			return new_theme.bg;
		else if (bg > ColorIndex::END_256 && bg > new_max_color)
			return new_theme.bg;

		return bg;
	};

	m_cursor.setDefaultColors(new_theme.fg, new_theme.bg);
	m_cursor.setFgColor(m_cursor.attrs().fg);
	m_cursor.setBgColor(m_cursor.attrs().bg);

	for (auto &screen: {&m_screen, &m_saved_screen}) {

		{
			auto cursor = screen->getCachedCursor();
			cursor.setDefaultColors(new_theme.fg, new_theme.bg);
			cursor.setFgColor(cursor.attrs().fg);
			cursor.setBgColor(cursor.attrs().bg);
			screen->setCachedCursor(cursor);
		}

		for (auto &line: screen->rawLines()) {
			// iterate over raw glyph vector, in case there are
			// hidden (saved) columns existing
			for (auto &g: line.raw()) {
				if (g.isDummy())
					continue;

				g.fg = fixupFgColor(g.fg);
				g.bg = fixupBgColor(g.bg);
			}
		}
	}

	setAllDirty();
}

void Term::setupTabs() {
	clearAllTabs();
	for (auto i = config::TABSPACES; i < m_size.cols; i += config::TABSPACES)
		m_tabs[i] = true;
}

void Term::reset() {
	m_cursor = CursorState{};

	setupTabs();
	resetScrollArea();
	m_mode.reset();
	m_mode.set({Mode::WRAP, Mode::UTF8});
	m_charsets.fill(Charset::USA);
	m_active_charset = 0;
	m_last_cursor_pos = CharPos{};
	m_esc_handler.reset();

	// reset main and alt screen
	for (size_t i = 0; i < 2; i++) {
		m_screen.resetScrollBuffer();
		moveCursorTo(topLeft());
		cursorControl(CursorState::Control::SAVE);
		clearScreen();
		swapScreen();
	}
}

void Term::setDirty(LineSpan span) {
	if (m_screen.numLines() == 0)
		// not yet initialized
		return;

	clamp(span);

	for (int i = span.top; i <= span.bottom; i++)
		m_screen[i].setDirty(true);
}

void Term::resize(const TermSize new_size) {
	// remember the old size for updating new screen regions below
	const auto old_size = m_size;

	assert(new_size.valid());
	if (!new_size.valid()) {
		return;
	}

	// shift the screen view down to keep cursor where we expect it
	//
	// only do this if the current cursor row will be outside the limits
	// of the new terminal size.
	//
	// moveCursorTo() below will update the cursor position accordingly to
	// make things fit again.
	if (const auto shift = m_cursor.pos.y - new_size.rows + 1; shift > 0) {
		for (auto screen: {&m_screen, &m_saved_screen}) {
			screen->shiftViewDown(shift);
		}

		m_selection.scroll(0, -shift);
	}

	m_screen.saveScrollState();

	// adjust dimensions of internal data structures
	m_screen.setDimension(new_size, m_cursor.attrs());
	m_saved_screen.setDimension(new_size, m_saved_screen.getCachedCursor().attrs());

	// update terminal size (needed by setupTabs() below)
	m_size = new_size;

	// recalculate tab markers if we have more columns now
	if (new_size.cols > old_size.cols) {
		setupTabs();
	}

	// reset scrolling region
	resetScrollArea();
	// make use of the clamping in moveCursorTo() to get a valid cursor position again
	moveCursorTo(m_cursor.pos);

	// Clear both screens (it marks all lines dirty)
	const auto saved_cursor = m_cursor;

	for (size_t i = 0; i < 2; i++) {
		// clear new cols if number of columns increased
		if (old_size.cols < new_size.cols && old_size.rows > 0) {
			// we don't need to clear the Glyphs, they are
			// initialized in Screen::setDimension()
			setDirty(LineSpan{0, old_size.rows - 1});
		}

		// clear new rows if number of rows increased
		if (old_size.rows < new_size.rows && old_size.cols > 0) {
			clearRegion({CharPos{0, old_size.rows}, bottomRight()});
		}
		swapScreen();
		cursorControl(CursorState::Control::LOAD);
	}

	m_cursor = saved_cursor;

	// keep the current scrolling position even when changing the window
	// size. note that this only works if terminal output is disabled
	// (ctrl + s), otherwise e.g. the shell will attempt to reconstruct
	// its prompt, triggering a stopScrolling() in write().
	// to further improve this we would need to differentiate between user
	// input and application output in write(), while still allowing
	// application output to be added to the current screen without
	// changing the scrolling position.
	if (!m_screen.restoreScrollState()) {
		scrollHistoryUpMax();
		m_selection.reset();
	}

	if (m_screen.isScrolled()) {
		setAllDirty();
	}

}

void Term::clearRegion(Range range) {

	range.sanitize();
	range.clamp(bottomRight());

	for (auto pos = range.begin; pos.y <= range.end.y; pos.y++) {
		auto &line = m_screen[pos.y];
		line.setDirty(true);
		line.shrinkToPhysical();

		if (line.empty()) {
			// if this is a new line that has been scrolled into
			// view then we need to set it to proper size first
			line.resize(m_size.cols);
		}

		for (pos.x = range.begin.x; pos.x <= range.end.x; pos.x++) {
			if (m_selection.isSelected(pos))
				m_selection.reset();
			auto &gp = m_screen[pos];
			gp.clear(m_cursor.attrs());
		}
	}
}

void Term::clearLines(const LineSpan span) {
	clearRegion({CharPos{0, span.top}, CharPos{m_size.cols - 1, span.bottom}});
}

void Term::clearLinesBelowCursor() {
	if (isCursorAtBottom())
		// nothing to do
		return;

	const auto curpos = cursor().pos;
	clearRegion(Range{curpos.nextLine().startOfLine(), bottomRight()});
}

void Term::clearLinesAboveCursor() {
	if (isCursorAtTop())
		// nothing to do
		return;

	const auto curpos = cursor().pos;
	clearRegion(Range{topLeft(), atEndOfLine(curpos.prevLine())});
}

void Term::clearCursorLine() {
	const auto curpos = cursor().pos;
	clearLines(LineSpan{curpos.y, curpos.y});
}

void Term::clearColsBeforeCursor() {
	const auto curpos = cursor().pos;
	clearRegion({curpos.startOfLine(), curpos});
}

void Term::clearColsAfterCursor() {
	const auto curpos = cursor().pos;
	clearRegion(Range{curpos, atEndOfLine(curpos)});
}

bool Term::isCursorAtBottom() const {
	return cursor().pos.y == m_size.rows - 1;
}

bool Term::isCursorAtTop() const {
	return cursor().pos.y == 0;
}

void Term::setScrollArea(const LineSpan span) {
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

void Term::setAltScreen(const bool enable, const bool with_cursor, const bool clear_alt) {
	if (!m_allow_altscreen)
		return;

	// if the mode actually changed
	const bool need_switch = enable ^ onAltScreen();

	/*
	 * indeed original XTerm save/restores the cursor position again even
	 * if the screen is already set to the correct target
	 */

	if (with_cursor && enable)
		// save cursor before entering alt screen
		cursorControl(CursorState::Control::SAVE);

	if (need_switch) {
		// one control clears only upon leaving the alt screen,
		// another only upon entering it
		if (clear_alt && onAltScreen())
			clearScreen();
		swapScreen();
		if (clear_alt && onAltScreen())
			clearScreen();
	}

	if (with_cursor && !enable)
		// restore cursor when returning from alt screen
		cursorControl(CursorState::Control::LOAD);
}

void Term::swapScreen() {
	std::swap(m_screen, m_saved_screen);
	m_mode.flip(Mode::ALTSCREEN);
	setAllDirty();
}

void Term::cursorControl(const CursorState::Control ctrl) {

	switch(ctrl) {
		case CursorState::Control::SAVE:
			m_screen.setCachedCursor(m_cursor);
			break;
		case CursorState::Control::LOAD:
			m_cursor = m_screen.getCachedCursor();
			moveCursorTo(m_cursor.pos);
			break;
	}
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
	auto new_pos = m_cursor.pos;

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
	count = std::clamp(count, 0, lineSpaceLeft());

	const auto cursor = m_cursor.pos;
	const int dst = cursor.x;
	const int src = cursor.x + count;
	const int left = m_size.cols - src;
	auto &line = m_screen[cursor.y];

	if (left <= 0)
		return;

	// NOTE: in C++20 there will be std::shift_left for this

	// slide remaining line content count characters to the left
	std::memmove(&line[dst], &line[src], left * sizeof(Line::value_type));
	// clear count characters at end of line
	clearRegion(Range{
		CharPos{m_size.cols - count, cursor.y},
		Width{count}
	});

	if (line[dst].isDummy()) {
		// we delete only part of a wide character, shift the line for another column then
		deleteColsAfterCursor(1);
	}
}

void Term::deleteLinesBelowCursor(int count) {
	if (m_scroll_area.inRange(m_cursor.pos)) {
		scrollUp(count, m_cursor.pos.y);
	}
}

void Term::insertBlanksAfterCursor(int count) {
	count = std::clamp(count, 0, lineSpaceLeft());

	const auto cursor = m_cursor.pos;
	const int dst = cursor.x + count;
	const int src = cursor.x;
	const int left = m_size.cols - dst;
	auto &line = m_screen[cursor.y];

	if (left <= 0)
		return;

	// slide remaining line content count characters to the right
	std::memmove(&line[dst], &line[src], left * sizeof(Line::value_type));
	clearRegion(Range{
		CharPos{src, cursor.y},
		Width{count}
	});
}

void Term::insertBlankLinesBelowCursor(int count) {
	if (m_scroll_area.inRange(m_cursor.pos)) {
		scrollDown(count, m_cursor.pos.y);
	}
}

void Term::doLineFeed() {
	const auto curpos = cursor().pos;

	if (curpos.y == scrollArea().bottom) {
		scrollUp(1);
	} else {
		moveCursorTo(curpos.nextLine());
	}
}

void Term::doReverseLineFeed() {
	const auto curpos = cursor().pos;

	if (curpos.y == scrollArea().top) {
		scrollDown(1);
	} else {
		moveCursorTo(curpos.prevLine());
	}
}

void Term::scrollDown(int num_lines, std::optional<int> opt_origin) {
	const auto area = m_scroll_area;
	const int origin = opt_origin ? *opt_origin : area.top;

	num_lines = std::clamp(num_lines, 0, area.bottom - origin + 1);

	/* see scrollUp() */

	for (auto i = area.bottom + 1; i < m_size.rows; i++) {
		std::swap(m_screen[i], m_screen[i-num_lines]);
	}

	for (auto i = 0; i < origin; i++) {
		std::swap(m_screen[i], m_screen[i-num_lines]);
	}

	m_screen.shiftViewUp(num_lines);

	// clear the to-be-overwritten lines, which will end up at the top
	// after scrolling finished below.
	clearLines(LineSpan{origin, origin + num_lines - 1});
	setDirty(LineSpan{origin, area.bottom});
	m_selection.scroll(origin, num_lines);
}

void Term::scrollUp(int num_lines, std::optional<int> opt_origin) {
	const auto area = m_scroll_area;
	int origin = opt_origin ? *opt_origin : area.top;

	num_lines = std::clamp(num_lines, 0, area.bottom - origin + 1);

	/*
	 * shift non-scrolling top and bottom areas downward by `num_lines`.
	 * By doing so the upper `num_lines` from the scrolling area will
	 * become scroll history lines. The new bottom lines that are
	 * appearing will be the oldest lines found in the scroll history
	 * ring buffer, which will be cleared at the end.
	 *
	 * Finally the ring buffer view will be shifted by `num_lines`
	 * downwards to keep everything in place.
	 */

	for (auto i = origin - 1; i >= 0; i--) {
		std::swap(m_screen[i], m_screen[i+num_lines]);
	}

	for (auto i = m_size.rows - 1; i > area.bottom; i--) {
		std::swap(m_screen[i], m_screen[i+num_lines]);
	}

	m_screen.shiftViewDown(num_lines);

	clearLines(LineSpan{area.bottom - num_lines + 1, area.bottom});
	setDirty(LineSpan{origin, area.bottom});
	m_selection.scroll(origin, -num_lines);
}

void Term::scrollHistoryUpByPage(const float num_pages) {
	return scrollHistoryUpByLines(static_cast<int>(num_pages * m_size.rows));
}

void Term::scrollHistoryDownByPage(const float num_pages) {
	return scrollHistoryDownByLines(static_cast<int>(num_pages * m_size.rows));
}

void Term::scrollHistoryUpByLines(int num_lines) {
	if (onAltScreen())
		// scrollback buffer is only supported on the main screen
		return;

	num_lines = m_screen.scrollHistoryUp(num_lines);
	m_selection.scroll(0, num_lines);
	setAllDirty();
}

void Term::scrollHistoryDownByLines(int num_lines) {
	if (onAltScreen())
		return;

	num_lines = m_screen.scrollHistoryDown(num_lines);
	m_selection.scroll(0, -num_lines);
	setAllDirty();
}

void Term::scrollHistoryUpMax() {
	if (onAltScreen())
		return;

	if (auto num_lines = m_screen.scrollHistoryMax(); num_lines != 0) {
		m_selection.scroll(0, num_lines);
		setAllDirty();
	}
}

void Term::dumpLine(const CharPos pos) const {
	std::string enc_rune;

	auto left = lineLen(pos);
	const auto line = m_screen[pos.y];

	for (auto it = line.begin(); left != 0; it++, left--) {
		utf8::encode(it->rune, enc_rune);
		m_tty.printToIoFile(enc_rune);
	}
	m_tty.printToIoFile("\n");
}

bool Term::existsBlinkingGlyph() const {
	// NOTE: this test could probably be cheaper by keeping track of this
	// attribute when changing glyphs.

	for (auto &line: m_screen) {
		for (auto &glyph: line) {
			if (glyph.isBlinking()) {
				return true;
			}
		}
	}

	return false;
}

void Term::setDirtyByAttr(const Glyph::Attr attr) {
	for (const auto &line: m_screen) {
		for (const auto &glyph: line) {
			if (glyph.mode[attr]) {
				line.setDirty(true);
				break;
			}
		}
	}
}

void Term::drawScreen() const {

	const Range range{topLeft(), bottomRight()};

	for (int y = range.begin.y; y <= range.end.y; y++) {
		auto &line = m_screen[y];

		if (!line.isDirty())
			continue;

		// beware that we need to point past the last valid position,
		// `range` has inclusive end coordinates!
		m_wsys.drawGlyphs(
				line.begin() + range.begin.x,
				line.begin() + raw_width(Rect{range}.width()),
				CharPos{range.begin.x, y});

		line.setDirty(false);
	}
}

void Term::drawCursor() const {
	/*
	 * we want to be able to draw the cursor even if we are currently
	 * scrolled back - if the cursor is still visible on the screen, that
	 * is.
	 *
	 * to achieve this we shift the cursor positions using the Screen
	 * helpers accordingly.
	 */
	const auto orig_last_pos = m_screen.shiftedPos(m_last_cursor_pos);
	auto shifted_last_pos = orig_last_pos;

	CharPos new_pos;
	if (auto shifted_new_pos = m_screen.shiftedPos(m_cursor.pos); shifted_new_pos) {
		new_pos = *shifted_new_pos;
	} else {
		// cursor isn't visible
		return;
	}

	// in case we point to a wide character dummy position, move one
	// character to the left to point to the actual character
	if (shifted_last_pos) {
		clampToScreen(*shifted_last_pos);
		if (m_screen[*shifted_last_pos].isDummy())
			shifted_last_pos->moveLeft();
	}

	if (m_screen[new_pos].isDummy())
		new_pos.moveLeft();

	if (shifted_last_pos) {
		m_wsys.clearCursor(*shifted_last_pos, m_screen[*shifted_last_pos]);
	}
	m_wsys.drawCursor(new_pos, m_screen[new_pos]);

	if (auto new_unshifted_pos = m_screen.unshiftedPos(new_pos); new_unshifted_pos) {
		m_last_cursor_pos = *new_unshifted_pos;
	}

	if (const bool cursor_pos_changed = orig_last_pos != new_pos; cursor_pos_changed) {
		m_wsys.setInputSpot(new_pos);
	}
}

void Term::draw() {
	if (!m_wsys.canDraw())
		return;

	drawScreen();
	drawCursor();
	m_wsys.finishDraw();
}

Rune Term::translateChar(Rune rune) const {
	// GRAPHIC0 translation table for VT100 "special graphics mode"
	//
	// The table is proudly stolen from rxvt.
	//

	constexpr auto VT100_GR_START = 0x41;
	constexpr auto VT100_GR_END   = 0x7e;

	// these have to be strings, not characters, because these are
	// multi-byte characters that don't fit into char.
	constexpr std::string_view VT100_0[VT100_GR_END - VT100_GR_START + 1] = { // 0x41 - 0x7e
		u8"↑", u8"↓", u8"→", u8"←", u8"█", u8"▚", u8"☃",        // A - G
		   {},    {},    {},    {},    {},    {},    {},    {}, // H - O
		   {},    {},    {},    {},    {},    {},    {},    {}, // P - W
		   {},    {},    {},    {},    {},    {},    {}, u8" ", // X - _
		u8"◆", u8"▒", u8"␉", u8"␌", u8"␍", u8"␊", u8"°", u8"±", // ` - g
		u8"␤", u8"␋", u8"┘", u8"┐", u8"┌", u8"└", u8"┼", u8"⎺", // h - o
		u8"⎻", u8"─", u8"⎼", u8"⎽", u8"├", u8"┤", u8"┴", u8"┬", // p - w
		u8"│", u8"≤", u8"≥", u8"π", u8"≠", u8"£", u8"·",        // x - ~
	};

	switch (m_charsets[m_active_charset]) {
		// nothing to do or not implemented
		default: break;
		case Charset::GRAPHIC0:
			 if (cosmos::in_range(rune, VT100_GR_START, VT100_GR_END)) {
				 const auto TRANS_CHAR = VT100_0[rune - VT100_GR_START];

				 if (!TRANS_CHAR.empty()) {
					utf8::decode(TRANS_CHAR, rune);
				 }
			 }
			 break;
	}

	return rune;
}

void Term::setChar(const Rune rune, const CharPos pos) {
	auto &glyph = m_screen[pos];

	// if we replace a WIDE/DUMMY position then correct the sibling position
	if (glyph.isWide()) {
		if (!isAtEndOfLine(pos)) {
			auto &next_glyph = m_screen[pos.nextCol()];
			next_glyph.rune = ' ';
			next_glyph.resetDummy();
		}
	} else if (glyph.isDummy()) {
		auto &prev_glyph = m_screen[pos.prevCol()];
		prev_glyph.rune = ' ';
		prev_glyph.resetWide();
	}

	m_screen[pos.y].setDirty(true);
	glyph = m_cursor.attrs();
	glyph.rune = translateChar(rune);
}

void Term::runDECTest() {
	// DEC screen alignment test: fill screen with E characters
	for (auto pos = topLeft(); pos.y < m_size.rows; pos.y++) {
		for (pos.x = 0; pos.x < m_size.cols; pos.x++)
			setChar('E', pos);
	}
}

void Term::repeatChar(int count) {
	if (m_last_char == 0)
		// nothing to repeat
		return;

	const int max_repeat = m_size.cols * m_size.rows;

	count = std::min(count, max_repeat);

	while (count-- > 0)
		putChar(m_last_char);
}

void Term::putChar(const Rune rune) {
	const auto rinfo = RuneInfo{rune, m_mode[Term::Mode::UTF8]};

	if (isPrintMode()) {
		m_tty.printToIoFile(rinfo.encoded());
	}

	if (m_esc_handler.process(rinfo) == EscapeHandler::WasProcessed{true})
		// input was part of a special control sequence
		return;

	if (m_selection.isSelected(m_cursor.pos))
		m_selection.reset();

	Glyph *gp = curGlyph();

	// perform automatic line wrap, if necessary
	if (m_mode[Mode::WRAP] && m_cursor.needWrapNext()) {
		gp->setWrapped();
		moveToNewline();
		gp = curGlyph();
	}

	const auto req_width = rinfo.width();

	if (const auto left = lineSpaceLeft(); left < req_width) {
		// if this is a multi-column wide character that doesn't fit
		// into this line anymore then clear remaining existing
		// characters
		clearColsAfterCursor();
		moveToNewline();
		gp = curGlyph();
	}

	// shift any remaining Glyphs to the right
	if (m_mode[Mode::INSERT]) {
		if (const auto to_move = lineSpaceLeft() - req_width; to_move > 0) {
			std::memmove(gp + req_width, gp, to_move * sizeof(Line::value_type));
			gp->mode.reset();
		}
	}

	setChar(rune, m_cursor.pos);
	m_last_char = rune;
	const auto left_chars = lineSpaceLeft();

	if (rinfo.isWide()) {
		gp->setWide();

		// if there's only one left character space then we'd be
		// operating in a miniscule terminal size and cannot
		// properly display wide characters.

		if (left_chars > 1) {
			// mark the follow-up position as dummy
			auto &next = gp[1];

			// if we are overriding another wide character, clean
			// up the dummy follow-up
			if (next.isWide() && left_chars > 2) {
				auto &after_next = gp[2];
				after_next.rune = ' ';
				after_next.resetDummy();
			}

			next.makeDummy();
		}
	}

	if (left_chars > req_width) {
		moveCursorTo(m_cursor.pos.nextCol(req_width));
	} else {
		m_cursor.setWrapNext(true);
	}
}

size_t Term::write(const std::string_view data, const ShowCtrlChars show_ctrl) {
	Rune rune;
	size_t charsize = 0;
	const bool use_utf8 = m_mode[Mode::UTF8];

	// jump back to the current input screen upon entering new data
	//
	// if it is non-interactive input then we will return to the
	// scrolling position once any changes to the current screen
	// are complete.
	//
	// this allows to stay scrolled back while new output data is
	// appended.
	//
	// we also have to consider the current selection here. It will be
	// shifted if e.g. a newline is processed. If we return to the
	// previous scroll position then we also have to restore the selection
	// coordinates.
	const auto saved_scroll = m_screen.saveScrollState();
	if (m_keep_scroll_position && saved_scroll) {
		m_selection.saveRange();
	}
	m_screen.stopScrolling();

	for (size_t pos = 0; pos < data.size(); pos += charsize) {
		if (use_utf8) {
			// process a complete utf8 char
			charsize = utf8::decode(data.substr(pos), rune);
			if (charsize == 0)
				return pos;
		} else {
			rune = data[pos] & 0xFF;
			charsize = 1;
		}

		if (show_ctrl && RuneInfo::isControlChar(rune)) {
			// add symbolic annotation for control chars
			if (rune & 0x80) {
				rune &= 0x7f;
				putChar('^');
				putChar('[');
			} else if (!cosmos::in_list(static_cast<char>(rune), {'\n', '\r', '\t'})) {
				rune ^= 0x40;
				putChar('^');
			}
		}

		putChar(rune);
	}

	if (m_keep_scroll_position) {
		if (!m_screen.restoreScrollState()) {
			scrollHistoryUpMax();
		} else if (saved_scroll) {
			m_selection.restoreRange();
		}
	}

	return data.size();
}

void Term::stopScrolling() {
	if (m_screen.isScrolled()) {
		const auto shift = m_screen.stopScrolling();
		m_selection.scroll(0, -shift);
		setAllDirty();
	}
}

} // end ns
