#ifndef NST_SCREEN_HXX
#define NST_SCREEN_HXX

// C++
#include <vector>

// nst
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
class Screen :
		public std::vector<Line> {
protected: // functions

	auto& base() { return static_cast<std::vector<Line>&>(*this); }
	auto& base() const { return static_cast<const std::vector<Line>&>(*this); }

public: // functions

	Line& line(const CharPos pos)             { return base()[pos.y]; }
	const Line& line(const CharPos pos) const { return base()[pos.y]; }

	void setDimension(const TermSize size) {
		resize(size.rows);

		// resize each row to new width
		for (auto &row: *this) {
			row.resize(size.cols);
		}
	}

	size_type numCols() const {
		return empty() ? 0 : front().size();
	}
	size_type numLines() const {
		return size();
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

	Glyph& operator[](const CharPos p)             { return base()[p.y][p.x]; }
	const Glyph& operator[](const CharPos p) const { return base()[p.y][p.x]; }

	Line& operator[](size_type pos)             { return base()[pos]; }
	const Line& operator[](size_type pos) const { return base()[pos]; }
};

} // end ns

#endif // inc. guard
