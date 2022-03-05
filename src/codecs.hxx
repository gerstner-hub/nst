#ifndef NST_CODECS_HXX
#define NST_CODECS_HXX

// libc
#include <stddef.h>

// nst
#include "Glyph.hxx"

namespace nst::utf8 {

constexpr size_t UTF_SIZE = 4;

size_t decode(const char *, Rune *, size_t);
size_t encode(Rune u, char *c);
size_t validate(Rune *, size_t);

}

#endif // inc. guard
