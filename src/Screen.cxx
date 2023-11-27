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

	// if we don't have a scroll buffer then we are likely on the alt
	// screen and then we also don't do stunts with restoring colums lost
	// due to resize.
	const auto init_line = Line{/*keep_data_on_shrink=*/hasScrollBuffer()};

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

std::string Screen::asText() const {
	std::string ret;

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
			addLine(m_lines[line]);
		}
	} else {
		/* current screen wraps around the ring buffer */
		const auto end_screen_index = m_cur_pos + m_rows - m_lines.size();

		for (size_t line = end_screen_index; line < m_cur_pos; line++) {
			addLine(m_lines[line]);
		}

		// now use the smart iterator to add the remaining lines from
		// the screen that wrap around the buffer
		for (const auto &line: *this) {
			addLine(line);
		}
	}

	return ret;
}

} // end ns
