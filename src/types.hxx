#ifndef NST_TYPES_HXX
#define NST_TYPES_HXX

// stdlib
#include <algorithm>
#include <bitset>
#include <functional>
#include <stdexcept>
#include <string_view>

// Xlib
#include <X11/X.h>

// cosmos
#include "cosmos/BitMask.hxx"

// X++
#include "X++/types.hxx"

namespace nst {

/* this header contains smaller utility types used throughout the project */

/// baseclass for position or coordinate like types
/**
 * The template argument is solely for creating strongly typed variants of
 * this type that cannot interact with each other, because they are
 * semantically different.
 **/
template <typename T>
struct PosT {
	int x = 0;
	int y = 0;

	void set(int _x, int _y) {
		x = _x;
		y = _y;
	}

	auto clampX(int max) { return clampX(0, max); }

	int clampX(int min, int max) {
		x = std::clamp(x, min, max);
		return x;
	}

	auto clampY(int max) { return clampY(0, max); }

	int clampY(int min, int max) {
		y = std::clamp(y, min, max);
		return y;
	}

	PosT operator+(const PosT &other) const {
		auto ret = *this;
		ret.x += other.x;
		ret.y += other.y;
		return ret;
	}

	PosT operator-(const PosT &other) const {
		auto ret = *this;
		ret.x -= other.x;
		ret.y -= other.y;
		return ret;
	}

	bool operator==(const PosT &other) const {
		return x == other.x && y == other.y;
	}

	bool operator!=(const PosT &other) const {
		return !(*this == other);
	}
};

class char_pos_t;

/// represents a character position on the terminal in col/row units
struct CharPos : public PosT<char_pos_t> {
	CharPos nextCol(const int n=1)  const { return CharPos{x + n, y    }; }
	CharPos prevCol(const int n=1)  const { return CharPos{x - n, y    }; }
	CharPos nextLine(const int n=1) const { return CharPos{x,     y + n}; }
	CharPos prevLine(const int n=1) const { return CharPos{x,     y - n}; }
	CharPos startOfLine() const { return CharPos{0, y}; }

	CharPos& moveLeft(const int n=1) { x -= n; return *this; }
	CharPos& moveRight(const int n=1) { x += n; return *this; }
	CharPos& moveDown(const int n=1) { y += n; return *this; }
	CharPos& moveUp(const int n=1) { y -= n; return *this; }

	CharPos& moveToStartOfLine() { x = 0; return *this; }
};

class draw_pos_t;

/// represents a drawing position in a window in pixel units
struct DrawPos : public PosT<draw_pos_t> {
	auto& moveDown( int px) { y += px; return *this; }
	auto& moveUp(   int px) { y -= px; return *this; }
	auto& moveLeft( int px) { x -= px; return *this; }
	auto& moveRight(int px) { x += px; return *this; }

	DrawPos atBelow(int px) const { return DrawPos(*this).moveDown(px); }
	DrawPos atAbove(int px) const { return DrawPos(*this).moveUp(px); }
	DrawPos atLeft( int px) const { return DrawPos(*this).moveLeft(px); }
	DrawPos atRight(int px) const { return DrawPos(*this).moveRight(px); }

	operator xpp::Coord() const {
		return xpp::Coord{x, y};
	}
};

/// a rectangular range of characters between a begin and and end CharPos
/**
 * the begin and end coordinates are *inclusive*.
 **/
struct Range {
public: // data
	CharPos begin;
	CharPos end;

public: // types

	/// strong type to represent widths
	enum class Width : int {};
	/// strong type to represent heights
	enum class Height : int {};

public: // functions

	Range() = default;
	Range(const CharPos &b, const CharPos &e) : begin(b), end(e) {}
	Range(const CharPos &b, const Width &w) : Range(b, b) {
		end.x += static_cast<int>(w);
	}
	Range(const CharPos &b, const Height &h) : Range(b, b) {
		end.y += static_cast<int>(h);
	}

	void invalidate() { begin.x = -1; }
	bool isValid() const { return begin.x != -1; }

	Width width() const { return static_cast<Width>(end.x - begin.x + 1); }
	Height height() const { return static_cast<Height>(end.y - begin.y + 1); }

	void clamp(const CharPos &max) {
		begin.clampX(max.x);
		begin.clampY(max.y);

		end.clampX(max.x);
		end.clampY(max.y);
	}

	void sanitize() {
		if (begin.x > end.x)
			std::swap(begin.x, end.x);
		if (begin.y > end.y)
			std::swap(begin.y, end.y);
	}
};

/// represents the terminal size in character elements
struct TermSize {
public: // data

	int cols = 0;
	int rows = 0;

public: // functions

	void normalize() {
		cols = std::max(cols, 1);
		rows = std::max(rows, 1);
	}

	bool valid() const {
		return cols >= 1 && rows >= 1;
	}
};

/// a span over one or more terminal lines
struct LineSpan {
public: // data
	int top = 0;
	int bottom = 0;

public: // functions

	LineSpan() = default;
	LineSpan(int t, int b) : top(t), bottom(b) {}
	explicit LineSpan(const Range &r) : top(r.begin.y), bottom(r.end.y) {}

	void sanitize() {
		if (top > bottom) {
			std::swap(top, bottom);
		}
	}

	/// returns whether the given position's y coordinate in within this LineSpan range
	bool inRange(const CharPos &pos) const {
		return top <= pos.y && pos.y <= bottom;
	}
};

/// a span over one or more terminal columns
struct ColSpan {
	int left = 0;
	int right = 0;
};

//! a two-dimensional extent in pixels e.g. for characters, windows, ...
struct Extent {
	int width = 0;
	int height = 0;

	bool operator==(const Extent &o) const {
		return width == o.width && height == o.height;
	}

	bool operator!=(const Extent &o) const {
		return !(*this == o);
	}

	void assertPositive() const {
		if (width < 0 || height < 0) {
			throw(std::runtime_error("extent-positive assertion failed"));
		}
	}

	// TODO: consider switching to unsigned here and use xpp::Extent instead
	operator xpp::Extent() const {
		assertPositive();
		return xpp::Extent{static_cast<unsigned int>(width), static_cast<unsigned int>(height)};
	}
};

typedef std::function<void ()> Callback;

/* types used in nst_config.h */
struct KbdShortcut {
	unsigned int mod;
	KeySym keysym;
	Callback func;
};

struct MouseShortcut {
	unsigned int mod;
	unsigned int button;
	Callback func;
	bool  release;
};

struct Key {
	KeySym k;
	unsigned int mask = 0;
	std::string_view s = "";
	/* three-valued logic variables: 0 indifferent, 1 on, -1 off */
	signed char appkey = 0;    /* application keypad */
	signed char appcursor = 0; /* application cursor */
};

class PressedButtons : public std::bitset<11> {
public: // data

	static constexpr size_t NO_BUTTON = 12;
public:

	/// returns the position of the lowest button pressed, or NO_BUTTON
	size_t getFirstButton() const {
		for (size_t bit = 0; bit < size(); bit++) {
			if (this->test(bit))
				return bit + 1;
		}

		return NO_BUTTON;
	}

	bool valid(const size_t button) const {
		return button >= 1 && button <= size();
	}

	void setPressed(const size_t button) {
		this->set(button - 1, true);
	}

	void setReleased(const size_t button) {
		this->set(button - 1, false);
	}

	static bool isScrollWheel(const size_t button) {
		return button == 4 || button == 5;
	}
};

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

} // end ns

#endif // inc. guard
