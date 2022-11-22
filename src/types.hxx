#ifndef NST_TYPES_HXX
#define NST_TYPES_HXX

// stdlib
#include <algorithm>

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

} // end ns

#endif // inc. guard
