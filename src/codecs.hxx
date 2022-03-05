#ifndef NST_CODECS_HXX
#define NST_CODECS_HXX

// libc
#include <stddef.h>

// nst
#include "Glyph.hxx"

namespace nst::utf8 {

constexpr size_t UTF_SIZE = 4;

size_t decode(const char *, nst::Rune *, size_t);
size_t encode(nst::Rune u, char *c);
size_t validate(nst::Rune *, size_t);

}

#endif // inc. guard
