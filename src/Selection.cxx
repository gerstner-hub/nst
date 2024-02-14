// C++
#include <algorithm>
#include <cctype>

// cosmos
#include "cosmos/utils.hxx"
#include "cosmos/string.hxx"

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
	m_snap = Snap::NONE;
	m_snap_dir = Direction::FORWARD;
	m_orig.invalidate();
	m_term.setDirty(LineSpan{m_range});
	m_first_cont_extend = true;
}

bool Selection::hasScreenChanged() const {
	return m_alt_screen != m_term.onAltScreen();
}

bool Selection::isSelected(const CharPos pos) const {
	if (inEmptyState() || !m_orig.isValid() || hasScreenChanged())
		return false;
	else if (isRectType())
		return Rect{m_range}.inRect(pos);
	else // regular type
		return LinearRange{m_range}.inRange(pos);
}

void Selection::start(const CharPos pos, const Snap snap, const Direction dir) {
	clear();
	m_state = State::EMPTY;
	m_type = Type::REGULAR;
	m_alt_screen = m_term.onAltScreen();
	m_snap = snap;
	m_orig = Range{pos, pos};

	m_snap_dir = dir;

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

	if (isRegularType() && LinearRange{m_orig}.height() > Height{1}) {
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

void Selection::extendSnap() {
	if (m_snap == Snap::WORD_SEP) {
		if (tryExtendWordSep()) {
			return;
		} else if (m_snap_dir != Direction::BACKWARD) {
			// otherwise fall back to regular word extension
			m_snap = Snap::WORD;
		} else {
			// this is a special backwards search for WORD_SEP but
			// nothing was found, so give up.
			clear();
			return;
		}
	}

	extendSnap(m_range.begin, Direction::BACKWARD);
	extendSnap(m_range.end,   Direction::FORWARD);

	tryURISnap();
}

bool Selection::tryExtendWordSep() {
	// only do something if the clicked-on position is itself a separator.
	const auto &screen = m_term.screen();
	const auto &clicked = screen[m_range.begin];
	if (clicked.isDelimiter()) {
		if (m_snap_dir == Direction::FORWARD) {
			auto next = screen.nextInLine(m_range.begin);
			if (next) {
				m_range.begin = m_range.end = *next;
				extendWordSnap(m_range.end, m_snap_dir, clicked.rune);
				return true;
			}
		} else {
			auto prev = screen.prevInLine(m_range.begin);
			if (prev) {
				m_range.begin = m_range.end = *prev;
				extendWordSnap(m_range.begin, m_snap_dir, clicked.rune);
				return true;
			}
		}
	}

	return false;
}

void Selection::extendSnap(CharPos &pos, const Direction direction) const {
	switch (m_snap) {
		case Snap::NONE: return;
		case Snap::WORD_SEP: return; // this is specially handled in extendSnap()
		case Snap::WORD: return extendWordSnap(pos, direction);
		case Snap::LINE: return extendLineSnap(pos, direction);
	}
}

bool Selection::canExtendWord() const {
	return m_type == Type::REGULAR && m_snap == Snap::WORD && m_orig.isValid();
}

bool Selection::canExtendWordSep() const {
	return m_type == Type::REGULAR && m_snap == Snap::WORD_SEP && m_orig.isValid();
}

void Selection::tryContinueWordSnap(const CharPos pos) {
	if (!canExtendWord())
		return;
	else if (m_first_cont_extend) {
		// avoid further extending on the first double-click already
		m_first_cont_extend = false;
		m_state = State::IDLE;
		return;
	}

	const auto old_range = m_range;
	m_force_word_extend = true;

	if (const auto &range = LinearRange{m_range}; range.inRange(pos)) {
		extendSnap();
	} else if(range > pos) {
		extendWordSnap(m_range.begin, Direction::BACKWARD);
	} else {
		extendWordSnap(m_range.end, Direction::FORWARD);
	}

	m_force_word_extend = false;
	if (old_range != m_range) {
		m_term.setDirty(LineSpan{m_range});
	}
}

void Selection::tryContinueWordSepSnap() {
	if (!canExtendWordSep())
		return;
	else if (m_first_cont_extend) {
		// avoid further extending on the first double-click already
		m_first_cont_extend = false;
		m_state = State::IDLE;
		return;
	}

	const auto old_range = m_range;
	const auto &screen = m_term.screen();

	if (m_snap_dir == Direction::FORWARD) {
		if (const auto sep_pos = screen.nextInLine(m_range.end); !sep_pos)
			return;
		else if (const auto next = screen.nextInLine(*sep_pos); !next)
			return;
		else
			m_range.end = *next;

		const auto delim_pos = m_range.begin.prevCol();
		// take the start separator from begin.prevCol() so we make sure we really use the correct one
		extendWordSnap(m_range.end, m_snap_dir, screen[delim_pos].rune);
	} else {
		if (const auto sep_pos = screen.prevInLine(m_range.begin); !sep_pos)
			return;
		else if (const auto prev = screen.prevInLine(*sep_pos); !prev)
			return;
		else
			m_range.begin = *prev;

		const auto delim_pos = m_range.end.nextCol();
		extendWordSnap(m_range.begin, m_snap_dir, screen[delim_pos].rune);
	}

	if (old_range != m_range) {
		m_term.setDirty(LineSpan{m_range});
	}
}

void Selection::extendWordSnap(CharPos &pos, const Direction direction, std::optional<Rune> delimiter) const {
	const auto &screen = m_term.screen();
	const int move_offset = direction == Direction::FORWARD ? 1 : -1;
	// force at least on additional word, even if we are already at word
	// borders.
	bool force = m_force_word_extend;
	auto isDelim = [delimiter](const Glyph &g) -> bool {
		if (delimiter)
			return g.rune == *delimiter;
		else
			return g.isDelimiter();
	};

	const Glyph *prevgp = &screen[pos];
	bool prev_is_delim = isDelim(*prevgp);
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
		const bool is_delim = isDelim(gp);
		// if this is just a dummy position then we need to move on to the next
		if (!gp.isDummy()) {
			// we support selecting not only words but also sequences of the same delimiter.
			if (is_delim != prev_is_delim || (is_delim && !gp.isSameRune(*prevgp))) {
				if (!force)
					break;
				else
					prev_is_delim = isDelim(gp);
			}

			force = false;
		}

		pos = next;
		prevgp = &gp;
	}

	m_orig = m_range;
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

	m_orig = m_range;
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

void Selection::tryURISnap() {
	if (m_snap != Snap::WORD || m_type != Type::REGULAR)
		return;

	const auto screen = m_term.screen();
	constexpr Rune URI_SEP[] = {':', '/', '/'};

	auto pos = m_range.end;

	for (const auto sepchar: URI_SEP) {
		auto next = screen.nextInLine(pos);
		if (!next)
			return;
		pos = *next;
		if (screen[pos].rune != sepchar)
			return;
	}

	const auto protocol = cosmos::to_lower(selection());

	auto isURI = [](const std::string_view sv) -> bool {
		for (const auto scheme: config::SEL_URI_SCHEMES) {
			if (sv == scheme)
				return true;
		}

		return false;
	};

	if (!isURI(protocol))
		return;

	// best effort approach to extract valid URI characters without
	// relying on a fully blown library routine. let's see how well this
	// works in practice.
	const std::basic_string<Rune> URI_CHARS{
		'-', '.', '_', '~', ':', '/', '?', '#', '[', ']', '@', '!',
		'$', '&', '\'', '(', ')', '*', '+', ';', '%', '='
	};

	std::optional<CharPos> next;

	while ((next = screen.nextInLine(pos)) != std::nullopt) {
		const auto rune = screen[*next].rune;

		if (!std::isalnum(rune) && URI_CHARS.find_first_of(rune) == URI_CHARS.npos)
			break;

		pos = *next;
	}

	m_range.end = pos;
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
	 * of the scroll area (taking into account `origin_y`).
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
			normalizeRange();
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
		const size_t bufsize = (screen.numCols()+1) * raw_height(LinearRange{m_range}.height()) * utf8::UTF_SIZE;
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

	if (!config::SEL_LINE_SNAP_KEEP_NEWLINE && m_snap == Snap::LINE) {
		// removing trailing newlines if in line snap mode
		while (!ret.empty() && ret.back() == '\n')
			ret.pop_back();
	}

	return ret;
}

void Selection::dump() const {
	if (auto sel = selection(); !sel.empty()) {
		m_nst.tty().printToIoFile(sel);
	}
}

} // end ns
