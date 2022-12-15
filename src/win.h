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

void xbell(void);
void xclipcopy(void);
void xdrawcursor(int, int, Glyph, int, int, Glyph);
void xdrawline(const Line&, int, int, int);
void xfinishdraw(void);
void xloadcols(void);
int xsetcolorname(size_t, const char *);
void xseticontitle(const char *);
void xsettitle(const char *);
void xsetcursor(const CursorStyle &);
void xsetmode(bool, const WinMode &);
void xsetpointermotion(bool);
void xsetsel(char *);
bool xstartdraw();
void xximspot(const CharPos&);
int xgetcolor(size_t x, unsigned char *r, unsigned char *g, unsigned char *b);

}

#endif // inc. guard
