#ifndef NST_CODECS_HXX
#define NST_CODECS_HXX

// libc
#include <stddef.h>

// C++
#include <string>
#include <string_view>

// nst
#include "Glyph.hxx"

namespace nst::utf8 {

constexpr size_t UTF_SIZE = 4;

size_t decode(const char *, Rune *, size_t);
size_t encode(Rune u, char *c);
void encode(Rune u, std::string &s);
size_t validate(Rune *, size_t);

}

namespace nst::base64 {

/// decodes a base64 encoded string and returns a string containing the decoded result
std::string decode(const std::string_view &s);

}

#endif // inc. guard
