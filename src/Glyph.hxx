#ifndef NST_GLYPH_HXX
#define NST_GLYPH_HXX

// stdlib
#include <vector>

// libcosmos
#include "cosmos/BitMask.hxx"

namespace nst {

typedef uint_least32_t Rune;

struct Glyph {
public: // types

	enum class Attr : unsigned {
		NONE       = 0,
		BOLD       = 1 << 0,
		FAINT      = 1 << 1,
		ITALIC     = 1 << 2,
		UNDERLINE  = 1 << 3,
		BLINK      = 1 << 4,
		REVERSE    = 1 << 5,
		INVISIBLE  = 1 << 6,
		STRUCK     = 1 << 7,
		WRAP       = 1 << 8,
		WIDE       = 1 << 9,
		WDUMMY     = 1 << 10
	};

	typedef cosmos::BitMask<Attr> AttrBitMask;

	typedef uint32_t color_t;

public: // data

	Rune u = 0;       /* character code */
	AttrBitMask mode; /* attribute flags */
	color_t fg = 0;  /* foreground  */
	color_t bg = 0;  /* background  */

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
	color_t getBrightColor() const {
		return fg + 8;
	}

	static bool isTrueColor(const color_t c) {
		return 1 << 24 & c;
	}
};

using Line = std::vector<Glyph>;

} // end ns

#endif // inc. guard
