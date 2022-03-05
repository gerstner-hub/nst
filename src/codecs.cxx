// nst
#include "codecs.hxx"
#include "macros.hxx"
#include "Glyph.hxx"

// libcosmos
#include "cosmos/algs.hxx"

using cosmos::in_range;

namespace nst::utf8 {

typedef unsigned char utf8_t;

static constexpr utf8_t UTF_BYTE[UTF_SIZE + 1] = {0x80,    0, 0xC0, 0xE0, 0xF0};
static constexpr utf8_t UTF_MASK[UTF_SIZE + 1] = {0xC0, 0x80, 0xE0, 0xF0, 0xF8};

static constexpr Rune UTF_MIN[UTF_SIZE + 1] = {       0,    0,  0x80,  0x800,  0x10000};
static constexpr Rune UTF_MAX[UTF_SIZE + 1] = {0x10FFFF, 0x7F, 0x7FF, 0xFFFF, 0x10FFFF};

static constexpr Rune UTF_INVALID = 0xFFFD;

static char encodebyte(Rune u, size_t i) {
	return UTF_BYTE[i] | (u & ~UTF_MASK[i]);
}

static Rune decodebyte(char c, size_t &i) {
	auto byte = static_cast<utf8_t>(c);

	for (i = 0; i < cosmos::num_elements(UTF_MASK); ++i)
		if ((byte & UTF_MASK[i]) == UTF_BYTE[i])
			return byte & ~UTF_MASK[i];

	return 0;
}

size_t decode(const char *c, Rune *u, size_t clen) {
	*u = UTF_INVALID;
	if (!clen)
		return 0;
	size_t len, type;
	Rune udecoded = decodebyte(c[0], len);
	if (!in_range(len, 1, UTF_SIZE))
		return 1;

	size_t i, j;

	for (i = 1, j = 1; i < clen && j < len; ++i, ++j) {
		udecoded = (udecoded << 6) | decodebyte(c[i], type);
		if (type != 0)
			return j;
	}

	if (j < len)
		return 0;

	*u = udecoded;
	validate(u, len);

	return len;
}

size_t encode(Rune u, char *c) {
	const size_t len = validate(&u, 0);
	if (len > UTF_SIZE)
		return 0;

	for (size_t i = len - 1; i != 0; --i) {
		c[i] = encodebyte(u, 0);
		u >>= 6;
	}

	c[0] = encodebyte(u, len);

	return len;
}

size_t validate(Rune *u, size_t i) {
	if (!in_range(*u, UTF_MIN[i], UTF_MAX[i]) || in_range(*u, 0xD800, 0xDFFF))
		*u = UTF_INVALID;
	for (i = 1; *u > UTF_MAX[i]; ++i)
		;

	return i;
}

} // end ns
