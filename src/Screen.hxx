#ifndef NST_SCREEN_HXX
#define NST_SCREEN_HXX

// C++
#include <iterator>
#include <vector>

// nst
#include "CursorState.hxx"
#include "Glyph.hxx"
#include "Line.hxx"

namespace nst {

/// A terminal screen consisting of lines of Glyphs.
/**
 * This contains a two dimensional vector with some helper operations for the
 * Screen terminal context.
 *
 * Internally this is organized as a ring buffer holding also scrollback data.
 * The interface is such that only the currently visible screen can be
 * accessed. It is allowed to access lines beyond the screen view for
 * performing scroll operations, though, which e.g. happens in
 * Term::scrollUp().
 **/
class Screen {
public: // types

	/// Minimal iterator type for iterating over the current Screen content
	/**
	 * This is a simple iterator type that transparently handles the possible
	 * wrap-around in the ring buffer used in the Screen type.
	 *
	 * Offering random access iterator semantics is hard, because we often
	 * cannot determine the correct context and cannot offer operator< &
	 * friends due to the ring buffer's nature. Thus this only implements
	 * output iterator semantics.
	 **/
	template <class LV, class IT>
	class IteratorT {
	public: // types

		using iterator_category = std::output_iterator_tag;
		using difference_type = ptrdiff_t;
		using value_type = typename LV::value_type;
		using pointer = typename LV::pointer;
		using reference = typename LV::reference;

	public: // functions

		IteratorT(LV &lines, IT pos) :
			m_lines{lines}, m_pos{pos} {}

		auto& operator++() {
			if (++m_pos == m_lines.end()) {
				m_pos = m_lines.begin();
			}

			return *this;
		}

		auto operator++(int) {
			const auto ret = *this;
			++(*this);
			return ret;
		}

		auto operator+(const ptrdiff_t diff) {
			const auto left = m_lines.end() - m_pos - 1;

			if (diff <= left) {
				m_pos += diff;
			} else {
				m_pos = m_lines.begin() + diff - left - 1;
			}

			return *this;
		}

		bool operator==(const IteratorT &other) const {
			return m_pos == other.m_pos;
		}

		bool operator!=(const IteratorT &other) const {
			return !((*this) == other);
		}

		auto& operator*() {
			return *m_pos;
		}

		auto operator->() {
			return &(*m_pos);
		}

	protected: // data
		LV &m_lines; /// The vector representing the ring buffer
		IT m_pos; /// The current iterator position
	};

	using iterator = IteratorT<LineVector, LineVector::iterator>;
	using const_iterator = IteratorT<const LineVector, LineVector::const_iterator>;

public: // functions

	/// Create a new screen representation using the given number of history lines.
	/**
	 * If \c history_len is zero then no scrollback buffer will be
	 * provided.
	 **/
	explicit Screen(const size_t history_len) :
			m_history_len{history_len} {}

	size_t numCols() const {
		return m_lines.empty() ? 0 : m_lines.front().size();
	}
	size_t numLines() const {
		return m_rows;
	}

	bool validLine(const CharPos p) const {
		return p.y >= 0 && static_cast<size_t>(p.y) < numLines();
	}

	bool validColumn(const CharPos p) const {
		return p.x >= 0 && static_cast<size_t>(p.x) < numCols();
	}

	bool validPos(const CharPos p) const {
		return validLine(p) && validColumn(p);
	}

	Glyph& operator[](const CharPos p)             { return m_lines[translateLinePos(p.y)][p.x]; }
	const Glyph& operator[](const CharPos p) const { return m_lines[translateLinePos(p.y)][p.x]; }

	Line& operator[](ssize_t pos)             { return m_lines[translateLinePos(pos)]; }
	const Line& operator[](ssize_t pos) const { return m_lines[translateLinePos(pos)]; }

	auto begin() { return iterator{m_lines, m_lines.begin() + translateLinePos(0)}; }
	auto end() { return iterator{m_lines, m_lines.begin() + translateLinePos(m_rows)}; }
	auto begin() const { return const_iterator{m_lines, m_lines.begin() + translateLinePos(0)}; }
	auto end() const { return const_iterator{m_lines, m_lines.begin() + translateLinePos(m_rows)}; }

	void setCachedCursor(const CursorState &state) {
		m_cached_cursor = state;
	}

	auto& getCachedCursor() const {
		return m_cached_cursor;
	}

	bool hasScrollBuffer() const {
		return m_history_len != 0;
	}

	/// Change the current screen's dimensions.
	/**
	 * This will change the physical screen dimensions. It can be used for
	 * initial setup of the screen or for subsequent adaptions of the
	 * screen's size.
	 *
	 * When the screen becomes smaller then lines at the bottom that no
	 * longer fit the current screen will be deleted.
	 *
	 * If the number of columns is increased then new cells will be
	 * initialized using the provided \c defattrs. If the number of colums
	 * is decreased then lost cells will be deleted.
	 **/
	void setDimension(const TermSize size, const Glyph defattrs);

	/// Scrolls the screen content up to display older history.
	/**
	 * \return The number of lines actually scrolled.
	 **/
	size_t scrollHistoryUp(size_t lines) {
		if (!hasScrollBuffer())
			return 0;

		if (const auto left = m_lines.size() - m_rows - m_scroll_offset; lines > left) {
			lines = left;
		}

		ssize_t idx = ssize_t(lines);

		// find the first history line that actually has content
		while (idx > 0 && (*this)[-idx].empty())
			--idx;

		lines = size_t(idx);

		m_scroll_offset += lines;
		return lines;
	}

	/// Scrolls the screen content down to display newer history.
	/**
	 * \see scrollHistoryUp()
	 **/
	size_t scrollHistoryDown(size_t lines) {
		if (!hasScrollBuffer())
			return 0;

		if (lines > m_scroll_offset) {
			lines = m_scroll_offset;
		}

		m_scroll_offset -= lines;
		return lines;
	}

	/// Returns whether the screen is currently scrolled back to history.
	bool isScrolled() const {
		return m_scroll_offset != 0;
	}

	/// Reset the screen view to display the current screen contents.
	void stopScrolling() {
		m_scroll_offset = 0;
	}

	/// Shift the current screen view in the ring buffer up for the given number of lines.
	/**
	 * Term::scrollDown and Term::scrollUp use this for screen content
	 * scrolling.
	 **/
	void shiftViewUp(size_t lines) {
		if (lines <= m_cur_pos) {
			m_cur_pos -= lines;
		} else {
			lines -= m_cur_pos;
			m_cur_pos = m_lines.size() - lines;
		}
	}

	void shiftViewDown(size_t lines) {
		m_cur_pos += lines;
		if (m_cur_pos >= m_lines.size()) {
			m_cur_pos -= m_lines.size();
		}
	}

	/// Resets the scrolling data and ring buffer position.
	/**
	 * All scrollback history will be discarded along with the current
	 * screen content.
	 **/
	void resetScrollBuffer() {
		m_cur_pos = 0;
		m_scroll_offset = 0;
		// clear all lines except the (new) current screen
		for (auto it = m_lines.begin() + m_rows; it != m_lines.end(); it++) {
			it->clear();
		}
	}

protected: // functions

	/// Translates a line index on the screen into the proper index in the ring buffer in m_lines
	/**
	 * This supports negative input positions to refer to positions in the
	 * scroll history. The return type is unsigned and suitable for use
	 * with m_lines.
	 **/
	LineVector::size_type translateLinePos(ssize_t pos) const {
		pos += m_cur_pos;
		pos -= m_scroll_offset;

		while (pos < 0) {
			pos += m_lines.size();
		}

		while(static_cast<size_t>(pos) >= m_lines.size()) {
			pos -= m_lines.size();
		}

		return static_cast<LineVector::size_type>(pos);
	}

protected: // data

	LineVector m_lines; /// the actual ring buffer
	size_t m_rows = 0; /// number of rows the visible screen has.
	size_t m_cur_pos = 0; /// where the current screen content starts in the ring buffer.
	size_t m_scroll_offset = 0; /// how many lines we are currently scrolled back.
	size_t m_history_len = 0; /// how big the ring buffer for history should be (0 == no history)
	CursorState m_cached_cursor; /// save/load cursor state for this screen.
};

} // end ns

#endif // inc. guard
