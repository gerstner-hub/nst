#ifndef NST_GLYPH_HXX
#define NST_GLYPH_HXX

// cosmos
#include "cosmos/BitMask.hxx"

// nst
#include "types.hxx"

namespace nst {

/// Primitive integer type to store character codes to be displayed on the terminal.
using Rune = uint32_t;

/// Code, color and attribute information for a single character position on the terminal.
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

public: // data

	Rune rune = 0;    /// character code
	AttrBitMask mode; /// attribute flags
	ColorIndex fg = ColorIndex::INVALID; /// foreground color
	ColorIndex bg = ColorIndex::INVALID; /// background color

public: // functions

	bool isFgTrueColor() const {
		return is_true_color(fg);
	}
	bool isBgTrueColor() const {
		return is_true_color(bg);
	}
	bool featuresDiffer(const Glyph &other) const {
		return mode != other.mode || fg != other.fg || bg != other.bg;
	}
	bool needBrightColor() const {
		return mode[Attr::BOLD] && !mode[Attr::FAINT];
	}
	bool needFaintColor() const {
		return mode[Attr::FAINT] && !mode[Attr::BOLD];
	}
	bool isBasicColor() const {
		return fg <= ColorIndex::END_DIM_BASIC_COLOR;
	}
	ColorIndex toBrightColor() const {
		return ColorIndex{cosmos::to_integral(fg) + 8};
	}

	void clear(const Glyph &templ) {
		fg = templ.fg;
		bg = templ.bg;
		mode.reset();
		rune = ' ';
	}

	bool isSameRune(const Glyph &other) const {
		return rune == other.rune;
	}

	/// Replace all attributes by WDUMMY, reset rune
	void makeDummy() {
		mode = AttrBitMask{Attr::WDUMMY};
		rune = '\0';
	}

	/// returns whether the Glyph is "empty", currently meaning "space"
	bool isEmpty()      const { return rune == ' '; }
	bool hasValue()     const { return !isEmpty(); }
	bool isDummy()      const { return mode[Attr::WDUMMY]; }
	bool isWide()       const { return mode[Attr::WIDE]; }
	bool isWrapped()    const { return mode[Attr::WRAP]; }
	bool isUnderlined() const { return mode[Attr::UNDERLINE]; }
	bool isStruck()     const { return mode[Attr::STRUCK]; }
	bool isBlinking()   const { return mode[Attr::BLINK]; }

	void setWrapped()   { mode.set(Attr::WRAP); }
	void setWide()      { mode.set(Attr::WIDE); }

	void resetWide()    { mode.reset(Attr::WIDE); }
	void resetDummy()   { mode.reset(Attr::WDUMMY); }

	size_t width() const { return isWide() ? 2 : 1; }
};

/// Shorthand for attribute queries
using Attr = Glyph::Attr;

} // end ns

#endif // inc. guard
