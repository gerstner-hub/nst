#ifndef NST_ST_H
#define NST_ST_H

// stdlib
#include <cstring>

namespace nst {

template <typename T>
void setDefault(T &v, const T &def) {
	if (!v)
		v = def;
}

template <typename T, typename V>
inline void modifyBit(T &mask, const bool set, const V &bit) {
	if (set)
		mask |= bit;
	else
		mask &= ~bit;
}

}

#endif // inc. guard
