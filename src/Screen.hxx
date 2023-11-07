#ifndef NST_SCREEN_HXX
#define NST_SCREEN_HXX

// C++
#include <vector>

// nst
#include "CursorState.hxx"
#include "Glyph.hxx"

namespace nst {

/// a series of Glyphs forming a line on the terminal.
class Line :
		public std::vector<Glyph> {
public: // functions

	/// Returns whether the line has a WRAP attribute set for the last element
	bool isWrapped() const {
		return back().mode[Attr::WRAP];
	}

	bool isDirty() const {
		return m_dirty;
	}

	void setDirty(const bool dirty) const {
		m_dirty = dirty;
	}

protected: // data

	mutable bool m_dirty = false;
};

/// A terminal screen consisting of lines of Glyphs.
/**
 * This is a two dimensional vector with some helper operations for the
 * Screen terminal context.
 **/
class Screen {
public: // functions

	Line& line(const CharPos pos)             { return m_lines[pos.y]; }
	const Line& line(const CharPos pos) const { return m_lines[pos.y]; }

	void setDimension(const TermSize size) {
		m_lines.resize(size.rows);

		// resize each row to new width
		for (auto &row: m_lines) {
			row.resize(size.cols);
		}
	}

	size_t numCols() const {
		return m_lines.empty() ? 0 : m_lines.front().size();
	}
	size_t numLines() const {
		return m_lines.size();
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

	Glyph& operator[](const CharPos p)             { return m_lines[p.y][p.x]; }
	const Glyph& operator[](const CharPos p) const { return m_lines[p.y][p.x]; }

	Line& operator[](Line::size_type pos)             { return m_lines[pos]; }
	const Line& operator[](Line::size_type pos) const { return m_lines[pos]; }

	void setCachedCursor(const CursorState &state) {
		m_cached_cursor = state;
	}

	auto& getCachedCursor() const {
		return m_cached_cursor;
	}

	auto begin() { return m_lines.begin(); }
	auto end() { return m_lines.end(); }
	auto begin() const { return m_lines.begin(); }
	auto end() const { return m_lines.end(); }

protected: // data

	std::vector<Line> m_lines;
	CursorState m_cached_cursor; /// save/load cursor state for this screen.

};

} // end ns

#endif // inc. guard
