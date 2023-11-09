// cosmos
#include "cosmos/error/RuntimeError.hxx"

// nst
#include "Screen.hxx"

namespace nst {

void Screen::setDimension(const TermSize size, const Glyph defattrs) {

	if (m_history_len && size_t(size.rows) > m_history_len) {
		cosmos_throw( cosmos::RuntimeError{"Requested terminal size larger than scroll ring buffer"} );
	}

	// stop any active scrolling since the operations are destined for the
	// current screen
	stopScrolling();

	const auto old_rows = m_rows;
	m_rows = size.rows;

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
		const auto bufsize = m_history_len + m_rows + 1;
		m_lines.resize(bufsize);
	} else if (m_history_len == 0) {
		if (m_cur_pos != 0) {
			std::copy(this->begin(), this->end(), m_lines.begin());
			m_cur_pos = 0;
		}

		m_lines.resize(m_rows + 1);
	}

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

} // end ns
