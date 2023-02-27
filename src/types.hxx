#ifndef NST_TYPES_HXX
#define NST_TYPES_HXX

// C++
#include <algorithm>
#include <bitset>
#include <functional>
#include <stdexcept>
#include <string_view>

// X
#include <X11/X.h>

// cosmos
#include "cosmos/algs.hxx"
#include "cosmos/BitMask.hxx"

// X++
#include "X++/types.hxx"

namespace nst {

/**
 * @file
 *
 * This header contains simpler utility types used throughout the project.
 **/

/// Baseclass for position or coordinate like types.
/**
 * The template argument is solely for creating strongly typed variants of
 * this type that cannot interact with each other, because they are
 * semantically different.
 **/
template <typename T>
struct PosT {
public: // types
	int x = 0;
	int y = 0;

public: // functions

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

/// Represents a character position on the terminal in col/row units.
struct CharPos :
		public PosT<class char_pos_t> {
public: // functions
	CharPos nextCol(const int n=1)  const { return CharPos{x + n, y    }; }
	CharPos prevCol(const int n=1)  const { return CharPos{x - n, y    }; }
	CharPos nextLine(const int n=1) const { return CharPos{x,     y + n}; }
	CharPos prevLine(const int n=1) const { return CharPos{x,     y - n}; }
	CharPos startOfLine() const { return CharPos{0, y}; }

	CharPos& moveLeft(const int n=1)  { x -= n; return *this; }
	CharPos& moveRight(const int n=1) { x += n; return *this; }
	CharPos& moveDown(const int n=1)  { y += n; return *this; }
	CharPos& moveUp(const int n=1)    { y -= n; return *this; }

	CharPos& moveToStartOfLine() { x = 0; return *this; }
};

/// represents a drawing position in a window in pixel units
struct DrawPos :
		public PosT<class draw_pos_t> {
public: // functions
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

/// A rectangular range of characters between a begin and and end CharPos.
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

	Range(const CharPos &b, const CharPos &e) :
			begin{b}, end{e}
	{}

	Range(const CharPos &b, const Width &w) :
			Range{b, b} {
		end.x += static_cast<int>(w);
	}

	Range(const CharPos &b, const Height &h) :
			Range{b, b} {
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

	/// returns whether the given coordinate is within the current range setting
	bool inRange(const CharPos &pos) const {
		return
			cosmos::in_range(pos.y, begin.y, end.y) &&
			cosmos::in_range(pos.x, begin.x, end.x);
	}

	void scroll(int nlines) {
		begin.y += nlines;
		end.y += nlines;
	}
};

/// Represents the terminal size in character elements.
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

/// A span over one or more terminal lines.
struct LineSpan {
public: // data

	int top = 0;
	int bottom = 0;

public: // functions

	LineSpan() = default;
	LineSpan(int t, int b) :
			top{t}, bottom{b}
	{}
	explicit LineSpan(const Range &r) :
			top{r.begin.y}, bottom{r.end.y}
	{}

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

/// A span over one or more terminal columns.
struct ColSpan {
	int left = 0;
	int right = 0;
};

/// a two-dimensional extent in pixels e.g. for characters, windows etc.
struct Extent {
public: // data

	int width = 0;
	int height = 0;

public: // functions

	bool operator==(const Extent &o) const {
		return width == o.width && height == o.height;
	}

	bool operator!=(const Extent &o) const {
		return !(*this == o);
	}

	void assertPositive() const {
		if (width < 0 || height < 0) {
			throw std::runtime_error{"extent-positive assertion failed"};
		}
	}

	// TODO: consider switching to unsigned here and use xpp::Extent instead
	operator xpp::Extent() const {
		assertPositive();
		return xpp::Extent{static_cast<unsigned int>(width), static_cast<unsigned int>(height)};
	}
};


/* types used in nst_config.h */

using Callback = std::function<void ()>;

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
	std::string_view s{};
	/* three-valued logic variables: 0 indifferent, 1 on, -1 off */
	signed char appkey = 0;    /* application keypad */
	signed char appcursor = 0; /* application cursor */
};

class PressedButtons :
		public std::bitset<11> {
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

/// various X11 and drawing related window settings
enum class WinMode {
	VISIBLE     = 1 << 0,  /// whether the window is currently visible
	FOCUSED     = 1 << 1,  /// whether the window is currently focused
	APPKEYPAD   = 1 << 2,  /// keypad keys generate special events instead of numbers
	MOUSEBTN    = 1 << 3,  /// report mouse button press on TTY level
	MOUSEMOTION = 1 << 4,  /// report mouse motion events as CSI escape sequences on TTY level (if button pressed)
	REVERSE     = 1 << 5,  /// reverse front and background colors
	KBDLOCK     = 1 << 6,  /// the keyboard is locked (no input processed)
	HIDE_CURSOR = 1 << 7,  /// hide the cursor when rendering
	APPCURSOR   = 1 << 8,  /// cursor keys generate special events instead of ANSI escape codes
	MOUSE_SGR   = 1 << 9,  /// extended SGR (select graphic randition) mouse reporting
	EIGHT_BIT   = 1 << 10, /// handle meta key, set eighth bit of keyboard input (?)
	BLINK       = 1 << 11, /// whether blinking characters are currently shown or not
	FOCUS       = 1 << 12, /// whether X11 focus changes should be reported on TTY level
	MOUSEX10    = 1 << 13, /// X10 mouse backwards compatibility
	MOUSEMANY   = 1 << 14, /// report mouse motion events as CSI escape sequences independently of button press
	BRKT_PASTE  = 1 << 15, /// "bracketed" paste mode, an Xterm feature where pasted X selection is surrounded by special escape codes
	NUMLOCK     = 1 << 16, /// Numlock enable status, somehow interpreted for key mappings
	MOUSE       = MOUSEBTN|MOUSEMOTION|MOUSEX10|MOUSEMANY
};

using WinModeMask = cosmos::BitMask<WinMode>;

enum class CursorStyle {
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
