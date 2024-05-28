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
		m_nst{nst},
		m_term{nst.term()},
		m_word_delimiters{config::WORD_DELIMITERS},
		m_snap_keep_newline{config::SEL_LINE_SNAP_KEEP_NEWLINE}	{
	m_orig.invalidate();
	for (auto scheme: config::SEL_URI_SCHEMES) {
		m_uri_schemes.insert(std::string{scheme});
	}
}

void Selection::clear() {
	if (!m_orig.isValid())
		return;

	m_state = State::IDLE;
	m_snap = Snap::NONE;
	m_flags.reset();
	m_orig.invalidate();
	m_term.setDirty(LineSpan{m_range});
}

bool Selection::hasScreenChanged() const {
	return m_alt_screen != m_term.onAltScreen();
}

bool Selection::isDelimiter(const Glyph &g) const {
	return g.rune && m_word_delimiters.find_first_of(g.rune) != m_word_delimiters.npos;
}

void Selection::applyConfig() {
	auto &config = m_nst.configFile();

	if (const auto delimiters = config.asWideString("word_delimiters"); delimiters != std::nullopt) {
		m_word_delimiters = *delimiters;
	}

	if (const auto snap_keep_newline = config.asBool("snap_keep_newline"); snap_keep_newline != std::nullopt) {
		m_snap_keep_newline = *snap_keep_newline;
	}

	if (const auto uri_schemes = config.asString("selection_uri_schemes"); uri_schemes != std::nullopt) {
		m_uri_schemes.clear();

		for (const auto &scheme: cosmos::split(
					*uri_schemes, " ", cosmos::SplitFlags{cosmos::SplitFlag::STRIP_PARTS})) {
			m_uri_schemes.insert(scheme);
		}
	}
}

bool Selection::isSelected(const CharPos pos) const {
	if (inEmptyState() || !m_orig.isValid() || hasScreenChanged())
		return false;
	else if (isRectangular() || isFullLines())
		return Rect{m_range}.inRect(pos);
	else // regular type
		return LinearRange{m_range}.inRange(pos);
}

bool Selection::shouldStartNewSelection(const Snap snap, const Flags flags) const {

	if (m_snap == Snap::NONE && snap != Snap::NONE)
		return true;

	if (flags[Flag::BACKWARD]) {
		// will be handled during update()
		return false;
	} else if (flags[Flag::ALT_SNAP]) {
		// will be handled during update()
		return false;
	}

	return true;
}

void Selection::start(const CharPos pos, const Snap snap, const Flags flags) {

	if (!shouldStartNewSelection(snap, flags)) {
		return;
	}

	clear();

	m_state = State::EMPTY;
	m_alt_screen = m_term.onAltScreen();
	m_snap = snap;
	m_orig = Range{pos, pos};
	m_flags = flags;

	recalculate();

	m_term.setDirty(LineSpan{m_range});
}

void Selection::recalculate() {
	normalizeRange();

	if (isFinished()) {
		extendSnap();
	}
	extendLineBreaks();
	if (isFullLines()) {
		extendOverLine(m_range.begin, Direction::BACKWARD);
		extendOverLine(m_range.end, Direction::FORWARD);
	}
}

void Selection::update(const CharPos pos, const Flags flags) {
	const bool flags_changed = m_flags != flags;
	const auto old_range = m_range;

	if (flags[Flag::FINISHED] && !snapActive() && m_state == State::EMPTY) {
		clear();
		return;
	}

	if (flags_changed)
		m_flags = flags;

	if (forceExtendSnap()) {
		tryContinueWordSnap(pos);
		tryContinueSeparatorSnap();
	} else {
		extend(pos);
	}

	if (old_range != m_range || flags_changed) {
		m_term.setDirty(LineSpan{m_range});
		m_term.setDirty(LineSpan{old_range});
	}
}

void Selection::normalizeRange() {
	const auto begin = m_orig.begin;
	const auto end = m_orig.end;

	if (isRegular() && LinearRange{m_orig}.height() > Height{1}) {
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
	if (isRectangular() || isFullLines())
		return;

	// expand selection over line breaks for regular selection
	//
	// this extends the selection if the start or end coordinate points to
	// unassigned space.
	const auto start_line_len = m_term.lineLen(m_range.begin);
	const auto end_line_len   = m_term.lineLen(m_range.end);

	m_range.begin.x = std::min(m_range.begin.x, start_line_len);
	if (end_line_len <= m_range.end.x)
		m_range.end.x = m_term.numCols() - 1;
}

void Selection::extendSnap() {
	if (m_snap == Snap::SEPARATOR) {
		if (tryExtendSeparator()) {
			return;
		} else if (m_flags[Flag::BACKWARD]) {
			// otherwise fall back to regular word extension
			m_snap = Snap::WORD;
		} else {
			// this is a special backwards search for SEPARATOR but
			// nothing was found, so give up.
			clear();
			return;
		}
	} else if (m_snap == Snap::WORD) {
		extendWordSnap(m_range.begin, Direction::BACKWARD);
		extendWordSnap(m_range.end,   Direction::FORWARD);
		tryURISnap();
	}
}

bool Selection::tryExtendSeparator() {
	// only do something if the clicked-on position is itself a separator.
	const auto &screen = m_term.screen();
	const auto &clicked = screen[m_range.begin];
	if (isDelimiter(clicked)) {
		const auto snap_dir = currentSnapDir();

		if (snap_dir == Direction::FORWARD) {
			auto next = screen.nextInLine(m_range.begin);
			if (next) {
				m_range.begin = m_range.end = *next;
				extendWordSnap(m_range.end, snap_dir, clicked.rune);
				return true;
			}
		} else {
			auto prev = screen.prevInLine(m_range.begin);
			if (prev) {
				m_range.begin = m_range.end = *prev;
				extendWordSnap(m_range.begin, snap_dir, clicked.rune);
				return true;
			}
		}
	}

	return false;
}

bool Selection::canExtendWord() const {
	return !isRectangular() && m_snap == Snap::WORD && m_orig.isValid();
}

bool Selection::canExtendSeparator() const {
	return !isRectangular() && m_snap == Snap::SEPARATOR && m_orig.isValid();
}

void Selection::tryContinueWordSnap(const CharPos pos) {
	if (!canExtendWord())
		return;

	const auto old_range = m_range;

	if (const auto &range = LinearRange{m_range}; range.inRange(pos)) {
		extendSnap();
	} else if(range > pos) {
		extendWordSnap(m_range.begin, Direction::BACKWARD);
	} else {
		extendWordSnap(m_range.end, Direction::FORWARD);
	}

	if (old_range != m_range) {
		m_term.setDirty(LineSpan{m_range});
	}
}

void Selection::tryContinueSeparatorSnap() {
	if (!canExtendSeparator())
		return;

	const auto old_range = m_range;
	const auto &screen = m_term.screen();
	const auto snap_dir = currentSnapDir();

	if (snap_dir == Direction::FORWARD) {
		if (const auto sep_pos = screen.nextInLine(m_range.end); !sep_pos)
			return;
		else if (const auto next = screen.nextInLine(*sep_pos); !next)
			return;
		else
			m_range.end = *next;

		const auto delim_pos = m_range.begin.prevCol();
		// take the start separator from begin.prevCol() so we make sure we really use the correct one
		extendWordSnap(m_range.end, snap_dir, screen[delim_pos].rune);
	} else {
		if (const auto sep_pos = screen.prevInLine(m_range.begin); !sep_pos)
			return;
		else if (const auto prev = screen.prevInLine(*sep_pos); !prev)
			return;
		else
			m_range.begin = *prev;

		const auto delim_pos = m_range.end.nextCol();
		extendWordSnap(m_range.begin, snap_dir, screen[delim_pos].rune);
	}

	if (old_range != m_range) {
		m_term.setDirty(LineSpan{m_range});
	}
}

void Selection::extendWordSnap(CharPos &pos, const Direction direction, std::optional<Rune> delimiter) const {
	const auto &screen = m_term.screen();
	const int move_offset = direction == Direction::FORWARD ? 1 : -1;
	// force at least on additional word, even if we are already at word borders.
	bool force = forceExtendSnap();
	auto isDelim = [this, delimiter](const Glyph &g) -> bool {
		if (delimiter)
			return g.rune == *delimiter;
		else
			return isDelimiter(g);
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
}

void Selection::extendOverLine(CharPos &pos, const Direction direction) const {
	const auto &screen = m_term.screen();

	const auto last_col = m_term.numCols() - 1;
	const auto last_row = m_term.numRows() - 1;

	// Snap around if the the previous line or the current one has set
	// WRAP at its end. Then the whole next or previous line will be
	// selected.
	if (direction == Direction::FORWARD) {
		 // move to the end of the line, following wraps
		 pos.x = last_col;
		 for (; pos.y < last_row; pos.moveDown()) {
			 if (!screen[pos].isWrapped())
				 break;
		 }
	} else if (direction == Direction::BACKWARD) {
		 // move to the beginning of the line, following wraps
		 pos.x = 0;
		 for (; pos.y > 0; pos.moveUp()) {
			 if (!screen[pos.y-1][last_col].isWrapped())
				 break;
		 }
	}
}

void Selection::extend(const CharPos pos) {
	if (inIdleState()) {
		return;
	}

	m_orig.end = pos;
	recalculate();

	if (isFinished()) {
		m_state = State::IDLE;
		// we need to store the new coordinates for proper scroll()
		// behaviour. since the selection process is no longer active
		// we don't need the original coordinates any more.
		m_orig = m_range;
	} else if (!snapActive()) {
		m_state = State::READY;
	}
}

void Selection::tryURISnap() {
	if (m_snap != Snap::WORD || isRectangular())
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

	auto isURI = [this](const std::string_view sv) -> bool {
		for (const auto &scheme: m_uri_schemes) {
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
	/*
	 * do nothing if:
	 * - there are no selection coordinates
	 * - the selection is from the other screen
	 * - a selection process is still ongoing
	 */
	if (!m_orig.isValid() || hasScreenChanged() || !inIdleState())
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

		if (isRectangular() || isFullLines()) {
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
		if ((!is_last_line || lastx >= linelen) && (!last->isWrapped() || isRectangular()))
			ret.push_back('\n');
	}

	if (!m_snap_keep_newline && isFullLines()) {
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
