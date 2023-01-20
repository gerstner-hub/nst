#ifndef NST_HELPER_HXX
#define NST_HELPER_HXX

namespace nst {

template <typename T, typename V>
inline void modifyBit(T &mask, const bool set, const V &bit) {
	if (set)
		mask |= bit;
	else
		mask &= ~bit;
}

}

#endif // inc. guard
