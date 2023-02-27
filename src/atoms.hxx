#ifndef NST_ATOMS_HXX
#define NST_ATOMS_HXX

// X++
#include "X++/CachedAtom.hxx"

namespace nst::atoms {

inline constexpr xpp::CachedAtom xembed{"_XEMBED"};
inline constexpr xpp::CachedAtom incr{"INCR"};
inline constexpr xpp::CachedAtom netwmiconname{"_NET_WM_ICON_NAME"};
inline constexpr xpp::CachedAtom targets{"TARGETS"};

} // end ns

#endif // inc. guard
