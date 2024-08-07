#pragma once

// C++
#include <array>
#include <algorithm>
#include <climits>
#include <functional>
#include <stdexcept>
#include <string_view>

// cosmos
#include "cosmos/BitMask.hxx"
#include "cosmos/SysString.hxx"
#include "cosmos/utils.hxx"

// xpp
#include "xpp/types.hxx"
#include "xpp/keyboard.hxx"

namespace nst {

/**
 * @file
 *
 * This header contains simpler utility types used throughout the project.
 **/

/// Base class for position or coordinate like types.
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
	auto& moveLeft( const int n=1) { x -= n; return *this; }
	auto& moveRight(const int n=1) { x += n; return *this; }
	auto& moveDown( const int n=1) { y += n; return *this; }
	auto& moveUp(   const int n=1) { y -= n; return *this; }

	CharPos nextCol( const int n=1) const { return CharPos{x + n, y    }; }
	CharPos prevCol( const int n=1) const { return CharPos{x - n, y    }; }
	CharPos nextLine(const int n=1) const { return CharPos{x,     y + n}; }
	CharPos prevLine(const int n=1) const { return CharPos{x,     y - n}; }
	CharPos startOfLine() const { return CharPos{0, y}; }


	CharPos& moveToStartOfLine() { x = 0; return *this; }
};

/// represents a drawing position in a window in pixel units.
struct DrawPos :
		public PosT<class draw_pos_t> {
public: // functions
	auto& moveDown( const int px) { y += px; return *this; }
	auto& moveUp(   const int px) { y -= px; return *this; }
	auto& moveLeft( const int px) { x -= px; return *this; }
	auto& moveRight(const int px) { x += px; return *this; }

	DrawPos atBelow(const int px) const { return DrawPos{*this}.moveDown(px); }
	DrawPos atAbove(const int px) const { return DrawPos{*this}.moveUp(px); }
	DrawPos atLeft( const int px) const { return DrawPos{*this}.moveLeft(px); }
	DrawPos atRight(const int px) const { return DrawPos{*this}.moveRight(px); }

	operator xpp::Coord() const {
		return xpp::Coord{x, y};
	}

	DrawPos() = default;

	DrawPos(const int _x, const int _y) {
		set(_x, _y);
	}

	explicit DrawPos(const xpp::Coord coord) {
		set(coord.x, coord.y);
	}
};

/// strong type to represent widths
enum class Width  : int {};
/// strong type to represent heights
enum class Height : int {};

static inline auto raw_width  = cosmos::to_integral<Width>;
static inline auto raw_height = cosmos::to_integral<Height>;

/// A range of characters between a begin and an end CharPos.
/**
 * A range between a begin and an end coordinate. The exact meaning depends
 * upon the actual use case. It can be used together with Rect or LinearRange.
 *
 * The begin and end coordinates are *inclusive*.
 *
 * \todo TODO: this inclusiveness is problematic and unintuitive, see the
 *             `- 1` subtractions below.
 **/
struct Range {
public: // data
	CharPos begin;
	CharPos end;

public: // types

public: // functions

	Range() = default;

	Range(const CharPos b, const CharPos e) :
			begin{b}, end{e}
	{}

	Range(const CharPos b, const Width w) :
			Range{b, b} {
		end.x += raw_width(w) - 1;
	}

	Range(const CharPos b, const Height h) :
			Range{b, b} {
		end.y += raw_height(h) - 1;
	}

	void invalidate() { begin.x = -1; }
	bool isValid() const { return begin.x != -1; }

	void clamp(const CharPos max) {
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

	bool operator==(const Range &other) const {
		return begin == other.begin && end == other.end;
	}

	bool operator!=(const Range &other) const {
		return !(*this == other);
	}

	void scroll(const int nlines) {
		begin.y += nlines;
		end.y += nlines;
	}
};

/// A rectangular area defined by a begin and end coordinate.
struct Rect {
public: // functions

	explicit Rect(const Range &r) : m_range{r} {}

	Width width() const   { return  Width{std::abs(m_range.end.x - m_range.begin.x) + 1}; }
	Height height() const { return Height{std::abs(m_range.end.y - m_range.begin.y) + 1}; }

	/// Checks whether `pos` is within the rectangular area.
	bool inRect(const CharPos pos) const {
		return
			pos.x >= m_range.begin.x && pos.x <= m_range.end.x &&
			pos.y >= m_range.begin.y && pos.y <= m_range.end.y;
	}

protected: // data

	Range m_range;
};

/// A linear range defined by a begin and end coordinate.
/**
 * This defines a contiguous range of characters. All lines in-between the
 * begin and end coordinate are part of the range:
 *
 * ```
 * |    B--------|
 * |-------------|
 * |--------E    |
 * ```
 **/
struct LinearRange {
public: // functions

	explicit LinearRange(const Range &r) : m_range{r} {}

	/// Returns whether the given coordinate is within the current range setting.
	/**
	 * This includes the full lines in-between the begin and end position
	 * of the range.
	 **/
	bool inRange(const CharPos pos) const {
		if (pos.y < m_range.begin.y || pos.y > m_range.end.y)
			return false;

		if (pos.y == m_range.begin.y && pos.x < m_range.begin.x)
			return false;

		if (pos.y == m_range.end.y && pos.x > m_range.end.x)
			return false;

		return true;
	}

	/// Height (number of lines) for the current linear range.
	Height height() const { return Height{std::abs(m_range.end.y - m_range.begin.y) + 1}; }

	/// Checks whether the given position is logically smaller than the current Range.
	/**
	 * This expects that the current Range is sanitize()'d i.e. the begin
	 * coordinate is actually smaller than the end coordinate.
	 *
	 * The comparison checks whether the end coordinate of the current
	 * range is appearing on an earlier line than `pos` or on an earlier
	 * column (if on the same line).
	 **/
	bool operator<(const CharPos pos) const {
		return m_range.end.y < pos.y ||
			(m_range.end.y == pos.y && m_range.end.x < pos.x);
	}

	bool operator>(const CharPos pos) const {
		return !(*this < pos) && !inRange(pos);
	}


protected: // data

	Range m_range;
};

/// Represents the terminal size in character elements.
struct TermSize {
public: // data

	int cols = 0;
	int rows = 0;

public: // functions

	bool valid() const {
		return cols >= 1 && rows >= 1;
	}
};

/// A span over a number of terminal lines.
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

	/// returns whether the given position's y coordinate is within this LineSpan range
	bool inRange(const CharPos pos) const {
		return top <= pos.y && pos.y <= bottom;
	}
};

/// A span over a number of terminal columns.
struct ColSpan {
	int left = 0;
	int right = 0;
};

/// A two-dimensional extent in pixels e.g. for character bounding box, window dimensions etc.
struct Extent {
public: // data

	int width = 0;
	int height = 0;

public: // functions

	Extent() = default;

	explicit Extent(const xpp::Extent ex) :
			width{ex.width < INT_MAX ? static_cast<int>(ex.width) : INT_MAX},
			height{ex.height < INT_MAX ? static_cast<int>(ex.height) : INT_MAX}
	{}

	explicit Extent(int p_width, int p_height) :
			width{p_width}, height{p_height}
	{}

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

	operator xpp::Extent() const {
		assertPositive();
		return xpp::Extent{static_cast<unsigned int>(width), static_cast<unsigned int>(height)};
	}
};

/// Primitive integer type to store character codes to be displayed on the terminal.
using Rune = uint32_t;

using InputCallback = std::function<void ()>;
using StopScrolling = cosmos::NamedBool<struct stop_scroll_t, true>;

struct KbdShortcut {
	xpp::InputMask mod;
	xpp::KeySymID keysym;
	InputCallback func;
	const std::string_view label = {}; ///< used for matching ConfigFile entries, can be an empty string.
	StopScrolling stop_scrolling = StopScrolling(false);
};

struct MouseShortcut {
	xpp::InputMask mod;
	xpp::Button button;
	InputCallback func;
	bool release;
	StopScrolling stop_scrolling = StopScrolling(false);
};

/// various X11 and drawing related window settings
enum class WinMode {
	VISIBLE     = 1 << 0,  ///< whether the window is currently visible
	FOCUSED     = 1 << 1,  ///< whether the window is currently focused
	APPKEYPAD   = 1 << 2,  ///< keypad keys generate special events instead of numbers
	MOUSEBTN    = 1 << 3,  ///< report mouse button press on TTY level
	MOUSEMOTION = 1 << 4,  ///< report mouse motion events as CSI escape sequences on TTY level (if button pressed)
	REVERSE     = 1 << 5,  ///< reverse front and background colors
	KBDLOCK     = 1 << 6,  ///< the keyboard is locked (no input processed)
	HIDE_CURSOR = 1 << 7,  ///< hide the cursor when rendering
	APPCURSOR   = 1 << 8,  ///< cursor keys generate special events instead of ANSI escape codes
	MOUSE_SGR   = 1 << 9,  ///< extended SGR (select graphic randition) mouse reporting
	EIGHT_BIT   = 1 << 10, ///< encode meta (ALT) key by setting eighth bit of input characters
	BLINK       = 1 << 11, ///< whether blinking characters are currently shown or not
	FOCUS       = 1 << 12, ///< whether X11 focus changes should be reported on TTY level
	MOUSEX10    = 1 << 13, ///< X10 mouse backwards compatibility
	MOUSEMANY   = 1 << 14, ///< report mouse motion events as CSI escape sequences independently of button press
	BRKT_PASTE  = 1 << 15, ///< "bracketed" paste mode, an Xterm feature where pasted X selection is surrounded by special escape codes
	NUMLOCK     = 1 << 16, ///< Numlock enable status, used for key binding interpretation
	MOUSE       = MOUSEBTN|MOUSEMOTION|MOUSEX10|MOUSEMANY
};

using WinModeMask = cosmos::BitMask<WinMode>;

/// Key binding configuration.
/**
 * This structure keeps state data that, if matched upon input events, will
 * cause the sending of the designated control sequence to the TTY.
 **/
struct Key {
public: // types

	enum class AppKeypad {
		DISABLED   = -1,
		IGNORE     =  0,
		ENABLED    =  1,
		NO_NUMLOCK =  2
	};

	enum class AppCursor {
		ENABLED =   1,
		IGNORE  =   0,
		DISABLED = -1
	};

public: // functions

	bool matchesAppKeypad(const WinModeMask mode) const {
		const auto appkey_enabled = mode[WinMode::APPKEYPAD];

		if (appkey_enabled && appkeypad == AppKeypad::DISABLED)
			return false;
		else if (!appkey_enabled && appkeypad == AppKeypad::ENABLED)
			return false;
		else if (mode[WinMode::NUMLOCK] && appkeypad == AppKeypad::NO_NUMLOCK)
			return false;

		return true;
	}

	bool matchesAppCursor(const WinModeMask mode) const {
		const auto appcursor_enabled = mode[WinMode::APPCURSOR];

		if (appcursor_enabled && appcursor == AppCursor::DISABLED)
			return false;
		else if(!appcursor_enabled && appcursor == AppCursor::ENABLED)
			return false;

		return true;
	}

	bool operator<(const Key &other) const {
		return id < other.id;
	}

public: // data

	xpp::KeySymID id;
	xpp::InputMask mask{};
	std::string_view seq{};
	AppKeypad appkeypad{AppKeypad::IGNORE};
	AppCursor appcursor{AppCursor::IGNORE};
};

/// Different cursor styles that can be configured.
enum class CursorStyle {
	BLINKING_BLOCK = 0,
	BLINKING_BLOCK_DEFAULT,
	STEADY_BLOCK, // "█"
	REVERSE_BLOCK, // uses the reverse colors of the Glyph the cursor is on
	BLINKING_UNDERLINE, // "_"
	STEADY_UNDERLINE,
	BLINKING_BAR, // "|"
	STEADY_BAR,
	SNOWMAN, // "☃"
	END
};

inline bool is_blinking_cursor(const CursorStyle style) {
	switch (style) {
		default:
			return false;
		case CursorStyle::BLINKING_BLOCK:
		case CursorStyle::BLINKING_BLOCK_DEFAULT:
		case CursorStyle::BLINKING_UNDERLINE:
		case CursorStyle::BLINKING_BAR:
			return true;
	}
}

/// Represents a terminal color index _or_ a 24 bit RGB true color value
/**
 * For terminal color indices the following ranges exist:
 *
 * 0 - 15: the 16 basic system colors supported by most terminals
 * 16 - 255: 256 color support known from XTerm. The end of the range contains
 *           extended greyscale colors.
 * >= 256: custom defined extended colors, see nst_config header
 *
 * On top of this a ColorIndex may also contain 24 bit RGB true color values.
 * This is indicated via a special bit position set in the upper byte that is
 * unused otherwise.
 * This repurposing of this type is unfortunate but saves a noticable amount
 * of memory, because the Glyph type carries a ColorIndex for foreground and
 * background color. If we would use a dedicated TrueColor type then the size
 * of the Glyph type would increase by at least 8 bytes. For large terminal
 * dimensions this can lead to an increase in memory use of over 100
 * Kilobytes. To maintain low overhead continue using this bit fiddling.
 *
 * Note: using a std::variant instead would add full type safety at the
 * expense of at least 4 bytes extra per color, which is the same cost.
 **/
enum class ColorIndex : uint32_t {
	INVALID             = UINT32_MAX,
	END_DIM_BASIC_COLOR = 7,
	START_256           = 16,
	START_GREYSCALE     = 6 * 6 * 6 + 16,
	END_256             = 255,
	START_EXTENDED      = 256,
	TRUE_COLOR_FLAG     = size_t{1 << 24}
};

inline ColorIndex operator-(const ColorIndex a, const ColorIndex b) {
	return ColorIndex{cosmos::to_integral(a) - cosmos::to_integral(b)};
}

/// Returns whether the given index actually represents a 24-bit RGB true color value
inline bool is_true_color(const ColorIndex idx) {
	auto raw = cosmos::to_integral(idx);
	auto flag = cosmos::to_integral(ColorIndex::TRUE_COLOR_FLAG);

	return (raw & flag) != 0;
}

/// Sets the true color flag for the given index
inline ColorIndex to_true_color(const ColorIndex idx) {
	auto raw = cosmos::to_integral(idx);
	raw |= cosmos::to_integral(ColorIndex::TRUE_COLOR_FLAG);
	return ColorIndex{raw};
}

struct Theme {
	std::string_view name;
	/// Basic terminal colors (16 first used in escape sequence)
	std::array<std::string_view, 16> basic_colors;
	/// Extended color palette beyond index 255.
	std::vector<std::string_view> extended_colors;

	/// Returns the color name for a color number taking into account extended color configuration.
	/**
	 * \return
	 * 	The according color name or an empty string if none is configured for the given index.
	 **/
	cosmos::SysString getColorName(const ColorIndex idx) const {

		if (auto raw = cosmos::to_integral(idx); raw < basic_colors.size()) {
			// returning the raw pointer here is okay since we know that
			// they're always null terminated.
			return basic_colors[raw].data();
		} else if (idx >= ColorIndex::START_EXTENDED) {
			const auto ext = cosmos::to_integral(idx - ColorIndex::START_EXTENDED);
			// check for extended colors
			if (ext < extended_colors.size())
				return extended_colors[ext].data();
		}

		// unassigned
		// NOTE: the libX functions that consume this are tolerant towards null pointers
		return {};
	}

	/* these are indices into COLORNAMES or EXTENDED_COLORS above */

	/// Default foreground color.
	ColorIndex fg;
	/// Default background color.
	ColorIndex bg;
	/// Default cursor color.
	ColorIndex cursor_color;
	/// Default reverse color.
	ColorIndex reverse_cursor_color;
};

} // end ns
