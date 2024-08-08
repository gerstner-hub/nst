// C++
#include <algorithm>
#include <cctype>

// cosmos
#include "cosmos/string.hxx"
#include "cosmos/utils.hxx"

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
		m_line_paste_keep_newline{config::LINE_PASTE_KEEP_NEWLINE}	{
	m_orig.invalidate();
	for (auto scheme: config::SEL_URI_SCHEMES) {
		m_uri_schemes.insert(std::string{scheme});
	}
}

void Selection::reset() {
	if (!existsSelection())
		return;

	m_state = State::IDLE;
	m_mode = Mode::CONT_RANGE;
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

	if (const auto keep_newline = config.asBool("line_paste_keep_newline"); keep_newline != std::nullopt) {
		m_line_paste_keep_newline = *keep_newline;
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
	if (inEmptyState() || !existsSelection() || hasScreenChanged())
		return false;
	else if (doRectRange() || doLineRange())
		return Rect{m_range}.inRect(pos);
	else // exact range
		return LinearRange{m_range}.inRange(pos);
}

bool Selection::allowNewSelection(const Mode mode, const Flags flags) const {

	if ((mode == Mode::WORD_SNAP || mode == Mode::SEP_SNAP) && !inSnapMode()) {
		// snap behaviour is newly requested, so start over in any case.
		return true;
	}

	if (flags[Flag::BACKWARD] || flags[Flag::ALT]) {
		// modifying an existing selection will be handled during
		// update() instead.
		return false;
	}

	return true;
}

void Selection::start(const CharPos pos, const Mode mode, const Flags flags) {

	if (!allowNewSelection(mode, flags)) {
		return;
	}

	reset();

	m_state = State::EMPTY;
	m_alt_screen = m_term.onAltScreen();
	m_mode = mode;
	m_orig = Range{pos, pos};
	m_flags = flags;

	calcRange();

	m_term.setDirty(LineSpan{m_range});
}

bool Selection::update(const CharPos pos, const Mode mode, const Flags flags) {
	const bool flags_changed = m_flags != flags;
	const bool mode_changed = allowModeChange() && m_mode != mode;
	const auto old_range = m_range;
	const auto old_state = m_state;
	const auto is_finished = flags[Flag::FINISHED];

	if (inIdleState() && inRangeMode())
		// once a range selection is finished, don't change anything
		return is_finished;
	else if (is_finished && !inSnapMode() && inEmptyState()) {
		// no selection was made at all, so reset state
		reset();
		return true;
	}


	if (flags_changed)
		m_flags = flags;
	if (mode_changed)
		m_mode = mode;

	if (allowExtendSnap()) {
		if (canExtendWordSnap()) {
			continueWordSnap(pos);
		} else if (canExtendSepSnap()) {
			continueSepSnap();
		}
	} else {
		if (inRangeMode()) {
			// extend to the new end position
			m_orig.end = pos;

			calcRange();
		}

		if (isFinished()) {
			// only now calculate an initial snap, if applicable
			calcSnap();

			if (!existsSelection()) {
				// this can happen if in snap mode nothing
				// could be selected.
				return is_finished;
			}

			m_state = State::IDLE;
			// we need to store the new coordinates for proper scroll()
			// behaviour. since the selection process is no longer active
			// we don't need the original coordinates any more.
			m_orig = m_range;
		} else if (!inSnapMode()) {
			// snap modes are never ready, either IDLE or EMPTY.
			m_state = State::READY;
		}
	}

	const auto range_changed = old_range != m_range;
	const auto state_changed = m_state != old_state;

	if (range_changed || state_changed || flags_changed || mode_changed) {
		m_term.setDirty(LineSpan{m_range});
		m_term.setDirty(LineSpan{old_range});
	}

	return is_finished;
}

void Selection::calcRange() {
	normalizeRange();

	if (doLineRange()) {
		extendLine(Direction::BACKWARD);
		extendLine(Direction::FORWARD);
	}

	if (doContRange()) {
		extendLineBreaks();
	}
}

void Selection::normalizeRange() {
	const auto begin = m_orig.begin;
	const auto end = m_orig.end;

	if (doContRange() && LinearRange{m_orig}.height() > Height{1}) {
		// exact selection over more than one line:
		// use the exact start column and end column
		m_range.begin.x = begin.y < end.y ? begin.x : end.x;
		m_range.end.x   = begin.y < end.y ? end.x   : begin.x;
	} else {
		// for rectangular style or single-line selections we only
		// need the min/max values
		m_range.begin.x = std::min(begin.x, end.x);
		m_range.end.x   = std::max(begin.x, end.x);
	}

	m_range.begin.y = std::min(begin.y, end.y);
	m_range.end.y   = std::max(begin.y, end.y);
}

void Selection::extendLineBreaks() {
	// expand selection over line breaks for regular selection
	//
	// this extends the selection over full rows if the start or end
	// coordinate points to unassigned space.
	const auto start_line_len = m_term.lineLen(m_range.begin);
	const auto end_line_len   = m_term.lineLen(m_range.end);

	m_range.begin.x = std::min(m_range.begin.x, start_line_len);
	if (end_line_len <= m_range.end.x)
		m_range.end.x = m_term.numCols() - 1;
}

void Selection::calcSnap() {
	if (doSepSnap()) {
		if (!extendToSep()) {
			// nothing was found, so give up.
			reset();
		}
	} else if (doWordSnap()) {
		extendWord(Direction::BACKWARD);
		extendWord(Direction::FORWARD);
		tryURISnap();
	}
}

bool Selection::extendToSep() {
	const auto &screen = m_term.screen();
	const auto &clicked = screen[m_range.begin];

	// only do something if the clicked-on position is itself a separator.
	if (isDelimiter(clicked)) {
		const auto snap_dir = snapDirection();

		if (snap_dir == Direction::FORWARD) {
			auto next = screen.nextInLine(m_range.begin);
			if (next) {
				m_range.begin = m_range.end = *next;
				extendWord(snap_dir, clicked.rune);
				return true;
			}
		} else {
			auto prev = screen.prevInLine(m_range.begin);
			if (prev) {
				m_range.begin = m_range.end = *prev;
				extendWord(snap_dir, clicked.rune);
				return true;
			}
		}
	}

	return false;
}

void Selection::continueWordSnap(const CharPos pos) {
	const auto old_range = m_range;

	if (const auto &range = LinearRange{m_range}; range.inRange(pos)) {
		// clicked on the selected word itself, expand in both directions
		extendWord(Direction::BACKWARD);
		extendWord(Direction::FORWARD);
	} else if(range > pos) {
		// clicked before / above the selected word, expand only backwards
		extendWord(Direction::BACKWARD);
	} else {
		// ditto forwards
		extendWord(Direction::FORWARD);
	}

	if (old_range != m_range) {
		m_term.setDirty(LineSpan{m_range});
	}
}

void Selection::continueSepSnap() {
	const auto old_range = m_range;
	const auto &screen = m_term.screen();
	const auto snap_dir = snapDirection();

	if (snap_dir == Direction::FORWARD) {
		if (const auto sep_pos = screen.nextInLine(m_range.end); !sep_pos)
			return;
		else if (const auto next = screen.nextInLine(*sep_pos); !next) {
			m_range.end = *sep_pos;
		} else {
			m_range.end = *next;
			extendWord(snap_dir, screen[*sep_pos].rune);
		}

	} else {
		if (const auto sep_pos = screen.prevInLine(m_range.begin); !sep_pos)
			return;
		else if (const auto prev = screen.prevInLine(*sep_pos); !prev) {
			m_range.begin = *sep_pos;
		} else {
			m_range.begin = *prev;
			extendWord(snap_dir, screen[*sep_pos].rune);
		}
	}

	if (old_range != m_range) {
		m_term.setDirty(LineSpan{m_range});
	}
}

void Selection::extendWord(const Direction direction, std::optional<Rune> delimiter) {
	auto &pos = direction == Direction::FORWARD ? m_range.end : m_range.begin;
	const auto &screen = m_term.screen();
	const int move_offset = direction == Direction::FORWARD ? 1 : -1;
	// extend at least on additional word, even if we are already at word borders.
	bool extend = allowExtendSnap();
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
				if (!extend)
					break;
				else
					prev_is_delim = isDelim(gp);
			}

			extend = false;
		}

		pos = next;
		prevgp = &gp;
	}
}

void Selection::extendLine(const Direction direction) {
	auto &pos = direction == Direction::FORWARD ? m_range.end : m_range.begin;
	const auto &screen = m_term.screen();

	const auto last_col = m_term.numCols() - 1;
	const auto last_row = m_term.numRows() - 1;

	// Snap around if the previous line or the current one has set
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

void Selection::tryURISnap() {
	const auto &screen = m_term.screen();
	constexpr Rune URI_SEPS[] = {':', '/', '/'};

	auto pos = m_range.end;

	for (const auto sepchar: URI_SEPS) {
		auto next = screen.nextInLine(pos);
		if (!next)
			return;
		pos = *next;
		if (screen[pos].rune != sepchar)
			return;
	}

	const auto protocol = cosmos::to_lower(data());

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
		reset();
	} else if (in_range(m_range.begin.y, origin_y, scroll_area.bottom)) {
		m_orig.scroll(num_lines);
		// if our selection is completely within the scroll area
		if (scroll_area.inRange(m_orig.begin) && scroll_area.inRange(m_orig.end)) {
			// adjust selection to new coordinates
			normalizeRange();
		} else {
			reset();
		}
	}
}

std::string Selection::data() const {
	if (!existsSelection())
		return "";

	const auto &screen = m_term.screen();
	std::string ret;

	{
		// worst case calculation for unicode text plus newlines
		const size_t bufsize = (screen.numCols()+1) * raw_height(LinearRange{m_range}.height()) * utf8::UTF_SIZE;
		ret.reserve(bufsize);
	}

	const Glyph *cur, *last;
	int endx = m_range.end.x;

	// append every set & selected glyph to the selection
	for (int y = m_range.begin.y; y <= m_range.end.y; y++) {
		const int linelen = m_term.lineLen(y);
		const bool is_last_line = m_range.end.y == y;

		if (linelen == 0) {
			ret.push_back('\n');
			continue;
		}

		if (doContRange()) {
			const bool is_first_line = m_range.begin.y == y;

			// in the exact selection case the begin/end column
			// coordinates are only relevant for the first/last
			// line, all lines in-between will be used completely

			cur = &screen[y][is_first_line ? m_range.begin.x : 0];
			endx = is_last_line ? m_range.end.x : screen.numCols() - 1;
		} else {
			// the start column is correct for the rectangular selection styles
			cur = &screen[y][m_range.begin.x];
		}

		last = &screen[y][std::min(endx, linelen-1)];
		// skip trailing spaces
		while (last >= cur && last->isEmpty())
			--last;

		for ( ; cur <= last; ++cur) {
			if (cur->isDummy())
				continue;

			utf8::encode(cur->rune, ret);
		}

		// Copy and pasting of line endings is inconsistent in the
		// inconsistent terminal and GUI world. The best solution
		// seems like to produce '\n' when something is copied from nst
		// and convert '\n' to '\r', when something to be pasted is
		// received by nst.
		if ((!is_last_line || endx >= linelen) && (!last->isWrapped() || doRectRange()))
			ret.push_back('\n');
	}

	if (doLineRange() && !m_line_paste_keep_newline) {
		// removing trailing newlines if so configured for line wise
		// selection mode
		while (!ret.empty() && ret.back() == '\n')
			ret.pop_back();
	}

	return ret;
}

void Selection::dump() const {
	if (const auto sel = data(); !sel.empty()) {
		m_nst.tty().printToIoFile(sel);
	}
}

} // end ns
