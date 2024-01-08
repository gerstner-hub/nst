// cosmos
#include "cosmos/error/RuntimeError.hxx"

// nst
#include "Screen.hxx"
#include "codecs.hxx"

namespace nst {

void Screen::setDimension(const TermSize size, const Glyph defattrs) {

	// stop any active scrolling since the operations are destined for the
	// current screen
	stopScrolling();

	// on the alt screen don't do stunts with restoring columns lost due to resize.
	const auto init_line = Line{/*keep_data_on_shrink=*/!m_is_alt_screen};

	/* if we use a ring buffer with scroll back history then never
	 * change the ring buffer's size, it will always stick at
	 * m_history_len. If there is no history len then we need to
	 * adjust the size to the current terminal dimensions though
	 * (e.g. for the alt screen).
	 */
	if (m_lines.empty()) {
		/* we need a buffer size of at least m_rows + 1 so that the
		 * custom iterator type works correctly, because we need a
		 * valid end() position that is not part of the current screen
		 */
		const auto bufsize = m_history_len + size.rows + 1;
		m_lines.resize(bufsize, init_line);
	} else if (m_history_len == 0) {
		if (m_cur_pos != 0) {
			std::copy(this->begin(), this->end(), m_lines.begin());
			m_cur_pos = 0;
		}

		m_lines.resize(size.rows + 1, init_line);
	} else if (size_t(size.rows) > m_lines.size()) {
		if (m_cur_pos == 0) {
			m_lines.resize(size.rows + m_history_len, init_line);
		} else {
			// we don't want to reorganize the ring buffer, the
			// scroll back buffer should be large enough to allow
			// larger window sizes
			cosmos_throw(cosmos::RuntimeError{"Requested terminal size larger than scroll ring buffer"});
		}
	}

	const auto old_rows = m_rows;
	m_rows = size.rows;

	// clear rows at the bottom that are no longer visible
	if (m_rows < old_rows && hasScrollBuffer()) {
		auto end = this->begin() + old_rows;
		for (auto it = this->begin() + m_rows; it != end; it++) {
			it->clear();
		}
	}

	if (size_t(size.cols) == this->begin()->size()) {
		return;
	}

	// unconditionally resize the visible screen to the new number
	// of cols, in case yet unallocated lines have come into view
	//
	// the initialization of newly appearing cells on the visible screen
	// will be done by the caller (in Term).
	for (auto &row: *this) {
		row.resize(size.cols, defattrs);
	}

	// in a second step resize any history lines in the ring buffer, but
	// only if they're already allocated
	for (auto &row: m_lines) {
		if (row.empty())
			continue;

		// when increasing the size then defattrs will be
		// applied to new columns
		row.resize(size.cols, defattrs);
	}
}

std::string Screen::asText(const CursorState &cursor) const {
	std::string ret;

	/*
	 * we want to skip empty lines shown on the current screen below the
	 * current cursor position. These are basically unallocated yet, but
	 * have been prepared for use by the Term logic.
	 *
	 * Determining this situation isn't all that easy:
	 *
	 * - don't do it on the alt screen
	 * - we need to receive the current screen line position
	 * - only if this position is beyond the current cursor position,
	 *   trigger the logic.
	 */
	auto reachedEndOfScreen = [&cursor,this](const std::optional<size_t> screen_pos) {
		if (m_is_alt_screen)
			return false;
		else if (!screen_pos)
			return false;
		// skip the current line which is helpful for `nst-msg -d | grep text`, to avoid matching the query itself
		else if (static_cast<int>(*screen_pos) < cursor.position().y)
			return false;
		else
			return true;
	};

	auto addLine = [&ret](const Line &line) {
		if (line.empty())
			return;

		const auto used_cols = line.usedLength();

		for (auto it = line.raw().begin(); it < line.raw().begin() + used_cols; it++) {
			utf8::encode(it->rune, ret);
		}
		utf8::encode(Rune{'\n'}, ret);
	};

	if (m_cur_pos + m_rows <= m_lines.size()) {
		for (size_t line = m_cur_pos + m_rows; line < m_lines.size(); line++) {
			addLine(m_lines[line]);
		}

		for (size_t line = 0; line < m_cur_pos + m_rows; line++) {
			if (reachedEndOfScreen(screenPos(line)))
				break;
			addLine(m_lines[line]);
		}
	} else {
		/* current screen wraps around the ring buffer */
		const auto end_screen_index = m_cur_pos + m_rows - m_lines.size();

		for (size_t line = end_screen_index; line < m_cur_pos; line++) {
			addLine(m_lines[line]);
		}

		size_t screen_index = 0;

		// now use the smart iterator to add the remaining lines from
		// the screen that wrap around the buffer
		for (const auto &line: *this) {
			if (reachedEndOfScreen(screen_index))
				break;
			addLine(line);
			screen_index++;
		}
	}

	return ret;
}

std::optional<size_t> Screen::screenPos(LineVector::size_type line_index) const {
	const auto screen_end = bufferPos(m_rows-1);
	const auto screen_wraps = screen_end < m_cur_pos;

	if (screen_wraps) {
		if (line_index >= m_cur_pos)
			return line_index - m_cur_pos;
		else if (line_index <= screen_end)
			return m_lines.size() - m_cur_pos + line_index;
	} else {
		if (line_index >= m_cur_pos && line_index < m_cur_pos + m_rows) {
			return line_index - m_cur_pos;
		}
	}

	return std::nullopt;
}

} // end ns
