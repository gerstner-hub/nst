#ifndef NST_GLYPH_HXX
#define NST_GLYPH_HXX

// C++
#include <vector>

// cosmos
#include "cosmos/BitMask.hxx"

// nst
#include "types.hxx"

namespace nst {

/// primitive integer type to store character codes to be displayed on the terminal
using Rune = uint_least32_t;

/// code, color and attribute information for a single character position on the terminal
struct Glyph {
public: // types

	/// Glyph rendering attributes
	enum class Attr {
		NONE       = 0,
		BOLD       = 1 << 0,
		FAINT      = 1 << 1,
		ITALIC     = 1 << 2,
		UNDERLINE  = 1 << 3,
		BLINK      = 1 << 4,
		REVERSE    = 1 << 5,
		INVISIBLE  = 1 << 6,
		STRUCK     = 1 << 7,
		WRAP       = 1 << 8, /// an automatic line wrap was inserted at this position (can only occur at the end of a line)
		WIDE       = 1 << 9, /// whether the Glyph spans multiple columns
		WDUMMY     = 1 << 10 /// for wide UTF8 characters this is a dummy placeholder position (a following, blocked column)
	};

	using AttrBitMask = cosmos::BitMask<Attr>;

	/// primitive integer type to store Glyph color information
	using color_t = uint32_t;

public: // data

	Rune u = 0;       /// character code
	AttrBitMask mode; /// attribute flags
	color_t fg = 0;   /// foreground color
	color_t bg = 0;   /// background color

public: // functions

	bool isFgTrueColor() const {
		return isTrueColor(fg);
	}
	bool isBgTrueColor() const {
		return isTrueColor(bg);
	}
	bool attrsDiffer(const Glyph &other) const {
		return mode != other.mode || fg != other.fg || bg != other.bg;
	}
	bool needBrightColor() const {
		return mode[Attr::BOLD] && !mode[Attr::FAINT];
	}
	bool needFaintColor() const {
		return mode[Attr::FAINT] && !mode[Attr::BOLD];
	}
	bool isBasicColor() const {
		return fg <= 7;
	}
	color_t toBrightColor() const {
		return fg + 8;
	}

	static bool isTrueColor(const color_t c) {
		return 1 << 24 & c;
	}

	void clear(const Glyph &templ) {
		fg = templ.fg;
		bg = templ.bg;
		mode.reset();
		u = ' ';
	}

	bool isSameRune(const Glyph &other) const {
		return u == other.u;
	}

	/// returns whether the Glyph is "empty", currently meaning "space"
	bool isEmpty() const { return u == ' '; }
	bool hasValue() const { return !isEmpty(); }
	bool isDummy() const { return mode[Attr::WDUMMY]; }
	bool isWide() const { return mode[Attr::WIDE]; }
	bool isWrapped() const { return mode[Attr::WRAP]; }
};

/// a series of Glyphs forming a line on the terminal
using Line = std::vector<Glyph>;

/// a terminal screen consisting of lines of Glyphs
class Screen :
		public std::vector<Line> {
protected: // functions

	auto& base() { return static_cast<std::vector<Line>&>(*this); }
	auto& base() const { return static_cast<const std::vector<Line>&>(*this); }

public: // functions

	Line& line(const CharPos &pos)             { return base()[pos.y]; }
	const Line& line(const CharPos &pos) const { return base()[pos.y]; }

	void setDimension(const TermSize &size) {
		resize(size.rows);

		/* resize each row to new width */
		for (auto &row: *this) {
			row.resize(size.cols);
		}
	}

	size_type numCols() const { return empty() ? 0 : front().size(); }
	size_type numLines() const { return size(); }

	bool validLine(const CharPos &p) const {
		return p.y >= 0 && static_cast<size_t>(p.y) < numLines();
	}

	bool validColumn(const CharPos &p) const {
		return p.x >= 0 && static_cast<size_t>(p.x) < numCols();
	}

	bool validPos(const CharPos &p) const {
		return validLine(p) && validColumn(p);
	}

	Glyph& operator[](const CharPos &p)             { return base()[p.y][p.x]; }
	const Glyph& operator[](const CharPos &p) const { return base()[p.y][p.x]; }

	Line& operator[](size_type pos)             { return base()[pos]; }
	const Line& operator[](size_type pos) const { return base()[pos]; }
};

} // end ns

#endif // inc. guard
