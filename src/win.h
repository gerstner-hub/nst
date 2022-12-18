#ifndef NST_WIN_H
#define NST_WIN_H

// cosmos
#include "cosmos/BitMask.hxx"

// nst
#include "Glyph.hxx"

namespace nst {

enum class WinMode {
	VISIBLE     = 1 << 0,
	FOCUSED     = 1 << 1,
	APPKEYPAD   = 1 << 2,
	MOUSEBTN    = 1 << 3,
	MOUSEMOTION = 1 << 4,
	REVERSE     = 1 << 5,
	KBDLOCK     = 1 << 6,
	HIDE        = 1 << 7,
	APPCURSOR   = 1 << 8,
	MOUSESGR    = 1 << 9,
	EIGHT_BIT   = 1 << 10,
	BLINK       = 1 << 11,
	FBLINK      = 1 << 12,
	FOCUS       = 1 << 13,
	MOUSEX10    = 1 << 14,
	MOUSEMANY   = 1 << 15,
	BRCKTPASTE  = 1 << 16,
	NUMLOCK     = 1 << 17,
	MOUSE       = MOUSEBTN|MOUSEMOTION|MOUSEX10|MOUSEMANY
};

typedef cosmos::BitMask<WinMode> WinModeMask;

enum class CursorStyle : unsigned {
	BLINKING_BLOCK = 0,
	BLINKING_BLOCK_DEFAULT,
	STEADY_BLOCK, // "█"
	BLINKING_UNDERLINE, // "_"
	STEADY_UNDERLINE,
	BLINKING_BAR, // "|"
	STEADY_BAR,
	SNOWMAN, // "☃"
	END
};

}

#endif // inc. guard
