// C++
#include <algorithm>

// cosmos
#include "cosmos/algs.hxx"

// nst
#include "codecs.hxx"
#include "Glyph.hxx"
#include "nst_config.hxx"
#include "nst.hxx"
#include "Selection.hxx"
#include "Term.hxx"

namespace nst {

Selection::Selection(Nst &nst) :
		m_nst{nst}, m_term{nst.term()} {
	m_orig.invalidate();
}

void Selection::clear() {
	if (!m_orig.isValid())
		return;

	m_state = State::IDLE;
	m_orig.invalidate();
	m_term.setDirty(LineSpan{m_range});
}

bool Selection::hasScreenChanged() const {
	return m_alt_screen != m_term.onAltScreen();
}

bool Selection::isSelected(const CharPos pos) const {
	if (inEmptyState() || !m_orig.isValid() || hasScreenChanged())
		return false;
	else if (isRectType())
		return m_range.inRange(pos);
	else // regular type
		// make sure it is a line between the start/end line and if pos
		// is exactly on the start or end line, make sure that the X
		// coordinate is in range
		return cosmos::in_range(pos.y, m_range.begin.y, m_range.end.y) &&
			(pos.y != m_range.begin.y || pos.x >= m_range.begin.x) &&
			(pos.y != m_range.end.y || pos.x <= m_range.end.x);
}

void Selection::start(const CharPos pos, const Snap snap) {
	clear();
	m_state = State::EMPTY;
	m_type = Type::REGULAR;
	m_alt_screen = m_term.onAltScreen();
	m_snap = snap;
	m_orig = Range{pos, pos};

	update();

	if (m_snap != Snap::NONE)
		m_state = State::READY;

	m_term.setDirty(LineSpan{m_range});
}

void Selection::update() {
	normalizeRange();
	extendSnap();
	extendLineBreaks();
}

void Selection::normalizeRange() {
	const auto begin = m_orig.begin;
	const auto end = m_orig.end;

	if (isRegularType() && m_orig.height() > Range::Height{1}) {
		// regular selection over more than one line:
		// use the correct start column and end column
		m_range.begin.x = begin.y < end.y ? begin.x : end.x;
		m_range.end.x   = begin.y < end.y ? end.x   : begin.x;
	} else {
		// for rectangular or single-line selections we only need the
		// min/max values
		m_range.begin.x = std::min(begin.x, end.x);
		m_range.end.x   = std::max(begin.x, end.x);
	}

	m_range.begin.y = std::min(begin.y, end.y);
	m_range.end.y   = std::max(begin.y, end.y);
}

void Selection::extendLineBreaks() {
	if (!isRegularType())
		return;

	// expand selection over line breaks for regular selection
	//
	// this makes sure that the start X coordinate is not larger than the
	// logic end of the start line. furthermore the end X coordinate will
	// be extended to the physical end of the line, if the logical end has
	// been exceeded by the selection.
	// It's not fully clear to my what the aim of that logic is.
	const auto start_line_len = m_term.lineLen(m_range.begin);
	const auto end_line_len   = m_term.lineLen(m_range.end);

	m_range.begin.x = std::min(m_range.begin.x, start_line_len);
	if (end_line_len <= m_range.end.x)
		m_range.end.x = m_term.numCols() - 1;
}

void Selection::extendSnap(CharPos &pos, const Direction direction) const {
	switch (m_snap) {
		case Snap::NONE: return;
		case Snap::WORD: return extendWordSnap(pos, direction);
		case Snap::LINE: return extendLineSnap(pos, direction);
	}
}

void Selection::extendWordSnap(CharPos &pos, const Direction direction) const {
	const auto &screen = m_term.screen();
	const int move_offset = direction == Direction::FORWARD ? 1 : -1;

	const Glyph *prevgp = &screen[pos];
	bool prev_is_delim = prevgp->isDelimiter();
	CharPos next;
	while (true) {
		next = pos.nextCol(move_offset);
		// Snap around if the word wraps around at the end or beginning of a line.
		if (!screen.validColumn(next)) {
			next = next.nextLine(move_offset);
			// move to end of previous line or beginning of next line
			next.x = next.x < 0 ? screen.numCols() - 1 : 0;

			if (!screen.validLine(next))
				// reached top or bottom of screen
				break;

			// inspect the final column if it wraps around
			const auto end_of_line = direction == Direction::FORWARD ? pos : next;
			if (!screen[end_of_line].isWrapped())
				// no need to wrap the selection around
				break;
		}

		if (next.x >= m_term.lineLen(next))
			// valid position but no valid character
			break;

		const Glyph &gp = screen[next];
		const bool is_delim = gp.isDelimiter();
		// if this is just a dummy position then we need to move on to the next
		if (!gp.isDummy()) {
			// we support selecting not only words but also sequences of the same delimiter.
			if (is_delim != prev_is_delim || (is_delim && !gp.isSameRune(*prevgp)))
				break;
		}

		pos = next;
		prevgp = &gp;
	}
}

void Selection::extendLineSnap(CharPos &pos, const Direction direction) const {
	const auto &screen = m_term.screen();

	const auto last_col = m_term.numCols() - 1;
	const auto last_row = m_term.numRows() - 1;

	// Snap around if the the previous line or the current one has set
	// WRAP at its end. Then the whole next or previous line will be
	// selected.
	switch (direction) {
	default: break;
	case Direction::FORWARD:
		 // move to the end of the line, following wraps
		 pos.x = last_col;
		 for (; pos.y < last_row; pos.moveDown()) {
			 if (!screen[pos].isWrapped())
				 break;
		 }
		 break;
	case Direction::BACKWARD:
		 // move to the beginning of the line, following wraps
		 pos.x = 0;
		 for (; pos.y > 0; pos.moveUp()) {
			 if (!screen[pos.y-1][last_col].isWrapped())
				 break;
		 }
		 break;
	};
}

void Selection::extend(const CharPos pos, const Type type, const bool done) {
	if (inIdleState()) {
		return;
	} else if (done && inEmptyState()) {
		clear();
		return;
	}

	const auto old_end = m_orig.end;
	const LineSpan old_normal_span{m_range};
	const auto oldtype = m_type;

	m_orig.end = pos;
	update();
	m_type = type;

	if (old_end != m_orig.end || oldtype != m_type || inEmptyState()) {
		const LineSpan new_normal_span{m_range};
		LineSpan dirty_span;
		dirty_span.top = std::min(new_normal_span.top, old_normal_span.top);
		dirty_span.bottom = std::max(new_normal_span.bottom, old_normal_span.bottom);
		m_term.setDirty(dirty_span);
	}

	m_state = done ? State::IDLE : State::READY;
}

void Selection::scroll(const int origin_y, const int num_lines) {
	if (!m_orig.isValid())
		return;

	const auto scroll_area = m_term.scrollArea();

	using cosmos::in_range;

	/*
	 * if the current selection is crossing the scroll area boundaries,
	 * clear it.
	 * an exception is when the selection crosses both the top and bottom
	 * boundary, the condition below will catch that and clear() as well.
	 *
	 * in summary: clear the selection if part of it is scrolled outside
	 * of the scroll area (taking into account `origin_y`.
	 */
	if (
			in_range(m_range.begin.y, origin_y, scroll_area.bottom) !=
			in_range(  m_range.end.y, origin_y, scroll_area.bottom)) {
		clear();
	} else if (in_range(m_range.begin.y, origin_y, scroll_area.bottom)) {
		m_orig.scroll(num_lines);
		// if our selection is completely within the scroll area
		if (scroll_area.inRange(m_orig.begin) && scroll_area.inRange(m_orig.end)) {
			// adjust selection to new coordinates
			update();
		} else {
			clear();
		}
	}
}

std::string Selection::selection() const {
	if (!m_orig.isValid())
		return "";

	const auto &screen = m_term.screen();
	std::string ret;

	{
		// worst case calculation for unicode text plus newlines
		const size_t bufsize = (screen.numCols()+1) * Range::raw_height(m_range.height()) * utf8::UTF_SIZE;
		ret.reserve(bufsize);
	}

	const Glyph *gp, *last;
	int lastx = m_range.end.x;

	// append every set & selected glyph to the selection
	for (int y = m_range.begin.y; y <= m_range.end.y; y++) {
		const int linelen = m_term.lineLen(y);
		const bool is_last_line = m_range.end.y == y;

		if (linelen == 0) {
			ret.push_back('\n');
			continue;
		}

		if (isRectType()) {
			// our selection range is correct for the rectangular
			// selection, only select the current line
			gp = &screen[y][m_range.begin.x];
		} else {
			const bool is_first_line = m_range.begin.y == y;

			// our selection's X coordinates are only relevant for
			// the first and last line, all lines in-between will
			// be used completely

			gp = &screen[y][is_first_line ? m_range.begin.x : 0];
			lastx = is_last_line ? m_range.end.x : screen.numCols() - 1;
		}

		last = &screen[y][std::min(lastx, linelen-1)];
		// skip trailing spaces
		while (last >= gp && last->isEmpty())
			--last;

		for ( ; gp <= last; ++gp) {
			if (gp->isDummy())
				continue;

			utf8::encode(gp->rune, ret);
		}

		// Copy and pasting of line endings is inconsistent in the
		// inconsistent terminal and GUI world. The best solution
		// seems like to produce '\n' when something is copied from nst
		// and convert '\n' to '\r', when something to be pasted is
		// received by nst.
		// FIXME: Fix the computer world.
		if ((!is_last_line || lastx >= linelen) && (!last->isWrapped() || isRectType()))
			ret.push_back('\n');
	}

	return ret;
}

void Selection::dump() const {
	if (auto sel = selection(); !sel.empty()) {
		m_nst.tty().printToIoFile(sel);
	}
}

} // end ns
