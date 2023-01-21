// stdlib
#include <cstring>

// nst
#include "codecs.hxx"
#include "Glyph.hxx"

// libcosmos
#include "cosmos/algs.hxx"

using cosmos::in_range;

namespace nst {

namespace utf8 {

typedef unsigned char utf8_t;

namespace {

constexpr utf8_t UTF_BYTE[UTF_SIZE + 1] = {0x80,    0, 0xC0, 0xE0, 0xF0};
constexpr utf8_t UTF_MASK[UTF_SIZE + 1] = {0xC0, 0x80, 0xE0, 0xF0, 0xF8};

constexpr Rune UTF_MIN[UTF_SIZE + 1] = {       0,    0,  0x80,  0x800,  0x10000};
constexpr Rune UTF_MAX[UTF_SIZE + 1] = {0x10FFFF, 0x7F, 0x7FF, 0xFFFF, 0x10FFFF};

constexpr Rune UTF_INVALID = 0xFFFD;

} // end anon ns

static char encodebyte(Rune u, size_t which) {
	return UTF_BYTE[which] | (u & ~UTF_MASK[which]);
}

static Rune decodebyte(const char c, size_t &i) {
	const auto byte = static_cast<utf8_t>(c);

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

void encode(Rune u, std::string &s) {
	const size_t len = validate(&u, 0);
	if (len > UTF_SIZE)
		return;

	const auto oldlen = s.length();

	s.resize(oldlen + len);
	auto c = s.data() + oldlen;

	for (size_t i = len - 1; i != 0; --i) {
		c[i] = encodebyte(u, 0);
		u >>= 6;
	}

	c[0] = encodebyte(u, len);
}

size_t validate(Rune *u, size_t i) {
	if (!in_range(*u, UTF_MIN[i], UTF_MAX[i]) || in_range(*u, 0xD800, 0xDFFF))
		*u = UTF_INVALID;
	for (i = 1; *u > UTF_MAX[i]; ++i)
		;

	return i;
}

} // end ns utf8

namespace base64 {

namespace {

constexpr char BASE64_DIGITS[] = {
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 62, 0, 0, 0,
	63, 52, 53, 54, 55, 56, 57, 58, 59, 60, 61, 0, 0, 0, -1, 0, 0, 0, 0, 1,
	2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21,
	22, 23, 24, 25, 0, 0, 0, 0, 0, 0, 26, 27, 28, 29, 30, 31, 32, 33, 34,
	35, 36, 37, 38, 39, 40, 41, 42, 43, 44, 45, 46, 47, 48, 49, 50, 51, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
};

} // end anon ns

std::string decode(const std::string_view &src) {
	std::string result;
	// + 3 is to consider padding that might be necessary if src.size() % 4 != 0.
	result.reserve(src.size() / 4 * 3 + 1 + 3);

	// returns the next base64 character from the input sequence
	auto nextchar = [&](auto &it) -> unsigned char {
		while (it != src.end() && !std::isprint(*it))
			it++;

		return it == src.end() ? '=' : *it++; /* emulate padding if string ends */
	};

	for (auto it = src.begin(); it != src.end(); it++) {
		const int a = BASE64_DIGITS[nextchar(it)];
		const int b = BASE64_DIGITS[nextchar(it)];
		const int c = BASE64_DIGITS[nextchar(it)];
		const int d = BASE64_DIGITS[nextchar(it)];

		/* invalid input. 'a' can be -1, e.g. if src is "\n" (c-str) */
		if (a == -1 || b == -1)
			break;

		char ch = (a << 2) | ((b & 0x30) >> 4);
		result.push_back(ch);

		if (c == -1)
			break;

		ch = ((b & 0x0f) << 4) | ((c & 0x3c) >> 2);
		result.push_back(ch);

		if (d == -1)
			break;
		ch = ((c & 0x03) << 6) | d;
		result.push_back(ch);
	}

	return result;
}

} // end ns base64

RuneInfo::RuneInfo(Rune r, const bool use_utf8) : m_rune(r) {
	m_is_control = isControlChar(r);

	if (r < 0x7f || !use_utf8) {
		// ascii case: keep single byte width and encoding length
		m_encoded[0] = r;
		return;
	}

	/* unicode case */

	// for non-control unicode characters check the display width
	if (!m_is_control) {
		// on error stick with a width of 1
		if (int w = ::wcwidth(r); w != -1) {
			m_width = w;
		}
	}

	m_enc_len = utf8::encode(r, m_encoded);
}

} // end ns
