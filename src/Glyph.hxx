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

public: // data

	Rune u = 0;       /* character code */
	AttrBitMask mode; /* attribute flags */
	uint32_t fg = 0;  /* foreground  */
	uint32_t bg = 0;  /* background  */

public: // functions

	bool isFgTrueColor() const {
		return 1 << 24 & fg;
	}
	bool isBgTrueColor() const {
		return 1 << 24 & bg;
	}
	bool attrsDiffer(const Glyph &other) const {
		return mode != other.mode || fg != other.fg || bg != other.bg;
	}
};

using Line = std::vector<Glyph>;

} // end ns

#endif // inc. guard
