// stdlib
#include <algorithm>

// libc
#include <string.h>

// cosmos
#include "cosmos/algs.hxx"

// nst
#include "codecs.hxx"
#include "Glyph.hxx"
#include "nst_config.hxx"
#include "nst.hxx"
#include "Selection.hxx"
#include "Term.hxx"
#include "TTY.hxx"

using cosmos::in_range;

namespace nst {

typedef Glyph::Attr Attr;

Selection::Selection(Nst &nst) : m_nst(nst), m_term(nst.getTerm()) {
	m_orig.invalidate();
}

bool Selection::isDelim(const Glyph &g) const {
	auto &DELIMITERS = config::WORDDELIMITERS;
	return g.u && DELIMITERS.find_first_of(g.u) != DELIMITERS.npos;
}

void Selection::clear() {
	if (!m_orig.isValid())
		return;

	m_mode = Mode::IDLE;
	m_orig.invalidate();
	m_term.setDirty(LineSpan{m_normal});
}

bool Selection::isSelected(const CharPos &pos) const {
	if (inEmptyMode() || !m_orig.isValid() || m_alt_screen != m_term.getMode().test(Term::Mode::ALTSCREEN))
		return false;
	else if (isRectType())
		return m_normal.inRange(pos);
	else
		return in_range(pos.y, m_normal.begin.y, m_normal.end.y) &&
			(pos.y != m_normal.begin.y || pos.x >= m_normal.begin.x) &&
			(pos.y != m_normal.end.y || pos.x <= m_normal.end.x);
}

void Selection::start(const CharPos &pos, Snap snap) {
	clear();
	m_mode = Mode::EMPTY;
	m_type = Type::REGULAR;
	m_alt_screen = m_term.getMode().test(Term::Mode::ALTSCREEN);
	m_snap = snap;
	m_orig.begin = pos;
	m_orig.end = pos;
	normalize();

	if (m_snap != Snap::NONE)
		m_mode = Mode::READY;

	m_term.setDirty(LineSpan{m_normal});
}

void Selection::normalize() {
	if (isRegularType() && m_orig.begin.y != m_orig.end.y) {
		m_normal.begin.x = m_orig.begin.y < m_orig.end.y ? m_orig.begin.x : m_orig.end.x;
		m_normal.end.x   = m_orig.begin.y < m_orig.end.y ? m_orig.end.x   : m_orig.begin.x;
	} else {
		m_normal.begin.x = std::min(m_orig.begin.x, m_orig.end.x);
		m_normal.end.x   = std::max(m_orig.begin.x, m_orig.end.x);
	}

	m_normal.begin.y = std::min(m_orig.begin.y, m_orig.end.y);
	m_normal.end.y   = std::max(m_orig.begin.y, m_orig.end.y);

	extendSnap(m_normal.begin, Direction::BACKWARD);
	extendSnap(m_normal.end,   Direction::FORWARD);

	/* expand selection over line breaks */
	if (isRectType())
		return;
	const auto len = m_term.getLineLen(m_normal.begin);
	m_normal.begin.x = std::min(m_normal.begin.x, len);
	if (m_term.getLineLen(m_normal.end) <= m_normal.end.x)
		m_normal.end.x = m_term.getNumCols() - 1;
}

void Selection::extendSnap(CharPos &pos, const Direction direction) const {
	switch (m_snap) {
		case Snap::NONE: return;
		case Snap::WORD: return extendWordSnap(pos, direction);
		case Snap::LINE: return extendLineSnap(pos, direction);
	}
}

void Selection::extendWordSnap(CharPos &pos, const Direction direction) const {
	const auto &screen = m_term.getScreen();
	const int move_offset = direction == Direction::FORWARD ? 1 : -1;

	const Glyph *prevgp = &screen[pos];
	bool prev_delim = isDelim(*prevgp);
	CharPos next;
	while(true) {
		next = pos.nextCol(move_offset);
		// Snap around if the word wraps around at the end or beginning of a line.
		if (!screen.validColumn(next)) {
			next.moveDown(move_offset);
			// move to end of previous line of beginning of next line
			next.x = next.x < 0 ? screen.numCols() - 1 : 0;

			if (!screen.validLine(next))
				// top or bottom of screen
				break;

			// inspect the final column if it wraps around
			const auto &end_of_line = direction == Direction::FORWARD ? pos : next;
			if (!screen[end_of_line].mode[Attr::WRAP])
				// no need to wrap the selection around
				break;
		}

		if (next.x >= m_term.getLineLen(next))
			// valid position but no valid character
			break;

		const Glyph &gp = screen[next];
		const bool delim = isDelim(gp);
		// if this is just a dummy position then we need to move on to the next
		if (!gp.isDummy()) {
			// we support selecting not only words but also
			// sequences of the same delimiter.
			if (delim != prev_delim || (delim && !gp.isSameRune(*prevgp)))
				break;
		}

		pos = next;
		prevgp = &gp;
		// TODO: this assignment should be unnecessary, since at this
		// point `delim` will never be distinct from `prev_delim`
		prev_delim = delim;
	}
}

void Selection::extendLineSnap(CharPos &pos, const Direction direction) const {
	const auto &screen = m_term.getScreen();

	const auto last_col = m_term.getNumCols() - 1;
	const auto last_row = m_term.getNumRows() - 1;

	/*
	 * Snap around if the the previous line or the current one
	 * has set WRAP at its end. Then the whole next or previous
	 * line will be selected.
	 */
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

void Selection::extend(const CharPos &pos, const Type type, const bool done) {
	if (inIdleMode()) {
		return;
	} else if (done && inEmptyMode()) {
		clear();
		return;
	}

	const auto old_end = m_orig.end;
	const LineSpan old_normal_span{m_normal};
	const auto oldtype = m_type;

	m_orig.end = pos;
	normalize();
	m_type = type;

	if (old_end != m_orig.end || oldtype != m_type || inEmptyMode()) {
		const LineSpan new_normal_span{m_normal};
		LineSpan dirty_span;
		dirty_span.top = std::min(new_normal_span.top, old_normal_span.top);
		dirty_span.bottom = std::max(new_normal_span.bottom, old_normal_span.bottom);
		m_term.setDirty(dirty_span);
	}

	m_mode = done ? Mode::IDLE : Mode::READY;
}

void Selection::scroll(const int origin_y, const int num_lines) {
	if (!m_orig.isValid())
		return;

	const auto scroll_area = m_term.getScrollArea();

	// not fully clear what this condition is for:
	// - either the selection range is completely within the scroll area
	// - or the selection range fully covers the scroll area and possibly more
	// but if only the top or end of our selection is outside the scroll
	// area we clear the selection
	if (
			in_range(m_normal.begin.y, origin_y, scroll_area.bottom) !=
			in_range(  m_normal.end.y, origin_y, scroll_area.bottom)) {
		clear();
	} else if (in_range(m_normal.begin.y, origin_y, scroll_area.bottom)) {
		m_orig.scroll(num_lines);
		// if our selection is completely within the scroll area
		if (scroll_area.inRange(m_orig.begin) && scroll_area.inRange(m_orig.end)) {
			// adjust selection to new coordinates
			normalize();
		} else {
			clear();
		}
	}
}

std::string Selection::getSelection() const {
	if (!m_orig.isValid())
		return "";

	const auto &screen = m_term.getScreen();
	const size_t bufsize = (m_term.getNumCols()+1) * (m_normal.end.y - m_normal.begin.y+1) * utf8::UTF_SIZE;
	std::string ret;
	const Glyph *gp, *last;
	int lastx, linelen;

	ret.reserve(bufsize);

	/* append every set & selected glyph to the selection */
	for (int y = m_normal.begin.y; y <= m_normal.end.y; y++) {
		if ((linelen = m_term.getLineLen(y)) == 0) {
			ret.push_back('\n');
			continue;
		}

		if (isRectType()) {
			gp = &screen[y][m_normal.begin.x];
			lastx = m_normal.end.x;
		} else {
			gp = &screen[y][m_normal.begin.y == y ? m_normal.begin.x : 0];
			lastx = (m_normal.end.y == y) ? m_normal.end.x : m_term.getNumCols() - 1;
		}
		last = &screen[y][std::min(lastx, linelen-1)];
		while (last >= gp && last->u == ' ')
			--last;

		for ( ; gp <= last; ++gp) {
			if (gp->mode.test(Attr::WDUMMY))
				continue;

			utf8::encode(gp->u, ret);
		}

		/*
		 * Copy and pasting of line endings is inconsistent
		 * in the inconsistent terminal and GUI world.
		 * The best solution seems like to produce '\n' when
		 * something is copied from st and convert '\n' to
		 * '\r', when something to be pasted is received by
		 * st.
		 * FIXME: Fix the computer world.
		 */
		if ((y < m_normal.end.y || lastx >= linelen) && (!(last->mode[Attr::WRAP]) || isRectType()))
			ret.push_back('\n');
	}

	return ret;
}

void Selection::dump() const {
	auto selection = getSelection();

	if (selection.empty())
		return;

	m_nst.getTTY().printToIoFile(selection);
}

} // end ns
