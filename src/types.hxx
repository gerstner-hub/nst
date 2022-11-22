#ifndef NST_TYPES_HXX
#define NST_TYPES_HXX

// stdlib
#include <algorithm>
#include <bitset>
#include <functional>

// libc
#include <limits.h>

// X11
#include <X11/X.h>

typedef unsigned char uchar;
typedef unsigned int uint;
typedef unsigned long ulong;
typedef unsigned short ushort;

namespace nst {

struct Coord {
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

	Coord operator+(const Coord &other) const {
		auto ret = *this;
		ret.x += other.x;
		ret.y += other.y;
		return ret;
	}

	Coord operator-(const Coord &other) const {
		auto ret = *this;
		ret.x -= other.x;
		ret.y -= other.y;
		return ret;
	}

	bool operator==(const Coord &other) const {
		return x == other.x && y == other.y;
	}

	bool operator!=(const Coord &other) const {
		return !(*this == other);
	}

	Coord nextCol(const int n=1)  const { return Coord{x + n, y    }; }
	Coord prevCol(const int n=1)  const { return Coord{x - n, y    }; }
	Coord nextLine(const int n=1) const { return Coord{x,     y + n}; }
	Coord prevLine(const int n=1) const { return Coord{x,     y - n}; }
};

struct Range {
	Coord begin;
	Coord end;

	void invalidate() { begin.x = -1; }
	bool isValid() const { return begin.x != -1; }

	void clamp(int max_x, int max_y) {
		begin.clampX(max_x);
		begin.clampY(max_y);

		end.clampX(max_x);
		end.clampY(max_y);
	}
};

typedef std::function<void ()> Callback;

/* types used in nst_config.h */
struct Shortcut {
	uint mod;
	KeySym keysym;
	Callback func;
};

struct MouseShortcut {
	uint mod;
	uint button;
	Callback func;
	bool  release;
};

struct Key {
	KeySym k;
	uint mask = 0;
	const char *s = nullptr;
	/* three-valued logic variables: 0 indifferent, 1 on, -1 off */
	signed char appkey = 0;    /* application keypad */
	signed char appcursor = 0; /* application cursor */
};

class PressedButtons : public std::bitset<11> {
public: // data

	static constexpr size_t NO_BUTTON = 12;
public:

	/// returns the position of the lowest button pressed
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
};

/* X modifiers */
#define XK_ANY_MOD    UINT_MAX
#define XK_NO_MOD     0
#define XK_SWITCH_MOD (1<<13|1<<14)

/* XEMBED messages */
#define XEMBED_FOCUS_IN  4
#define XEMBED_FOCUS_OUT 5

} // end ns

#endif // inc. guard
