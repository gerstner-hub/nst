// C
#include <wchar.h>

// C++
#include <cctype>

// cosmos
#include "cosmos/algs.hxx"

// nst
#include "codecs.hxx"
#include "Glyph.hxx"

namespace nst {

namespace utf8 {

/// primitive type used for individual UTF8 byte sequences
typedef unsigned char utf8_t;

namespace {

// utf8 byte sequence prefix bits, used for determining the type of byte in an utf8 sequence:
// - a follow-up byte starting with      0b10,    the 6 lower bits are data
// - a 1 byte leader byte, starting with 0b0,     the 7 lower bits are data (ASCII range)
// - a 2 byte leader byte, starting with 0b110,   the 5 lower bits are data
// - a 3 byte leader byte, starting with 0b1110,  the 4 lower bits are data
// - a 4 byte leader byte, starting with 0b11110, the 3 lower bits are data
constexpr utf8_t UTF_BYTE[UTF_SIZE + 1] = {0x80, 0x00, 0xC0, 0xE0, 0xF0};
constexpr utf8_t UTF_MASK[UTF_SIZE + 1] = {0xC0, 0x80, 0xE0, 0xF0, 0xF8};

// index for a detected trailing byte in UFT_BYTE / UTF_MASk above
constexpr size_t TRAILING_BYTE = 0;

// the maximum code point value ranges for sequences of one, two, three, four
// byte sequences.  for four byte sequences not all of the 21 bits make valid
// code points, thus the different maximum value. For the zero index
// (TRAILING_BYTE) these values don't actually make sense.
constexpr Rune UTF_MIN[UTF_SIZE + 1] = {       0,    0,  0x80,  0x800,  0x10000};
constexpr Rune UTF_MAX[UTF_SIZE + 1] = {0x10FFFF, 0x7F, 0x7FF, 0xFFFF, 0x10FFFF};

// this range of code points are surrogate characters only needed in UTF-16.
// They're invalid for utf-8.
constexpr Rune UTF_SURROGATE_START = 0xD800;
constexpr Rune UTF_SURROGATE_END   = 0xDFFF;

// replacement code point if anything goes wrong
constexpr Rune UTF_INVALID = 0xFFFD;

// encodes a single UTF-8 byte for an encoding byte sequence
// \c u should contain six bits of data for which == TRAILING_BYTE or the
// available number of bits depending on the leader byte for which > 0.
char encodebyte(const Rune rune, const size_t which) {
	return UTF_BYTE[which] | (rune & ~UTF_MASK[which]);
}

/// Decodes a single byte of a UTF8 byte sequence.
/**
 * \param[out] byte_nr the type of leader byte or TRAILING_BYTE (index into UTF_* tables)
 * \return the (partially decoded) bits for the code point.
 **/
Rune decodebyte(const char c, size_t &byte_nr) {
	const auto byte = static_cast<utf8_t>(c);

	for (byte_nr = 0; byte_nr < cosmos::num_elements(UTF_MASK); byte_nr++)
		if ((byte & UTF_MASK[byte_nr]) == UTF_BYTE[byte_nr])
			return byte & ~UTF_MASK[byte_nr];

	return 0;
}

/// Validates the given rune (code point).
/**
 * Checks whether the given rune is in a valid range of code points for the
 * number of input encoding bytes \c numbytes
 *
 * If validation fails then \c r will be set to UTF_INVALID.
 **/
void validate(Rune &r, size_t numbytes) {
	if (!cosmos::in_range(r, UTF_MIN[numbytes], UTF_MAX[numbytes]) ||
			cosmos::in_range(r, UTF_SURROGATE_START, UTF_SURROGATE_END)) {
		r = UTF_INVALID;
	}
}

/// Calculates the number of bytes needed to encode the given code point in utf8
/**
 * \return the number of bytes needed to encode \c r, or zero if it cannot be represented
 **/
size_t calc_bytes(const Rune r) {
	size_t numbytes;
	for (numbytes = 1; r > UTF_MAX[numbytes]; ++numbytes)
		;

	return numbytes <= UTF_SIZE ? numbytes : 0;
}

} // end anon ns

size_t decode(const std::string_view encoded, Rune &rune) {
	rune = UTF_INVALID;
	if (encoded.empty())
		return 0;
	size_t numbytes;
	Rune decoded = decodebyte(encoded[0], numbytes);
	if (!cosmos::in_range(numbytes, 1, UTF_SIZE))
		// an invalid start byte was encountered, discard it
		return 1;

	size_t curbyte = 1;
	size_t byte_type;

	for (; curbyte < encoded.length() && curbyte < numbytes; curbyte++) {
		// add six more bytes from each trailing byte
		decoded = (decoded << 6) | decodebyte(encoded[curbyte], byte_type);
		if (byte_type != TRAILING_BYTE)
			// less trailing bytes encountered than announced
			return curbyte;
	}

	// short input sequence
	if (curbyte < numbytes)
		// TODO: inconsistent error handling? Above 1 is returned for
		// an invalid start byte, but here after we processed an
		// incomplete sequence we return 0?
		return 0;

	rune = decoded;
	validate(rune, numbytes);

	return numbytes;
}

size_t encode(Rune rune, char out[UTF_SIZE]) {
	const size_t num_bytes = calc_bytes(rune);
	if (!num_bytes)
		return 0;

	for (size_t byte = num_bytes - 1; byte != 0; byte--) {
		out[byte] = encodebyte(rune, TRAILING_BYTE);
		// each trailing byte can encode 6 bits
		rune >>= 6;
	}

	// only now encode the leader byte at the beginning
	out[0] = encodebyte(rune, num_bytes);

	return num_bytes;
}

void encode(Rune rune, std::string &s) {
	const size_t num_bytes = calc_bytes(rune);
	if (!num_bytes) {
		return;
	}

	// only append to the output string
	const auto oldlen = s.length();

	s.resize(oldlen + num_bytes);
	auto out = s.data() + oldlen;

	for (size_t byte = num_bytes - 1; byte != 0; byte--) {
		out[byte] = encodebyte(rune, 0);
		rune >>= 6;
	}

	out[0] = encodebyte(rune, num_bytes);
}

} // end ns utf8

namespace base64 {

namespace {

constexpr char B64_PADDING = 0x7F;

// This table maps 8-bit ASCII characters to the corresponding base64 index.
// Unassigned values are set to zero.
constexpr uint8_t BASE64_DIGITS[] = {
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 62, 0, 0, 0,
	63, 52, 53, 54, 55, 56, 57, 58, 59, 60, 61, 0, 0, 0, B64_PADDING, 0, 0, 0, 0, 1,
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

std::string decode(const std::string_view src) {
	std::string result;
	// each four base64 digits make three bytes of output
	// + 3 is to consider padding that might be necessary if src.size() % 4 != 0.
	result.reserve(src.size() / 4 * 3 + 3);

	// returns the next base64 character from the input sequence
	auto nextchar = [&](auto &it) -> unsigned char {
		while (it != src.end() && !std::isprint(static_cast<unsigned char>(*it)))
			it++;

		return it == src.end() ? '=' : *it++; // emulate padding if string ends
	};

	for (auto it = src.begin(); it != src.end(); it++) {
		// we need four base64 digits of 6 bits each to decode 3 binary bytes
		const auto a = BASE64_DIGITS[nextchar(it)];
		const auto b = BASE64_DIGITS[nextchar(it)];
		const auto c = BASE64_DIGITS[nextchar(it)];
		const auto d = BASE64_DIGITS[nextchar(it)];

		// each base64 digit corresponds to 6 bits, so reconstruct the
		// data correspondingly

		// invalid input. 'a' can be -1, e.g. if src is "\n" (c-str)
		if (a == B64_PADDING || b == B64_PADDING)
			break;

		// the 6 bits from a plus the 2 upper bits of b form the first byte
		char ch = (a << 2) | ((b & 0x30) >> 4);
		result.push_back(ch);

		if (c == B64_PADDING)
			break;

		// the remaining 4 bits from b plus the 4 upper bits of c form the second byte
		ch = ((b & 0x0f) << 4) | ((c & 0x3c) >> 2);
		result.push_back(ch);

		if (d == B64_PADDING)
			break;
		// the remaining two bits from c plus the remaining 6 bits from d form the third byte
		ch = ((c & 0x03) << 6) | d;
		result.push_back(ch);
	}

	return result;
}

} // end ns base64

RuneInfo::RuneInfo(Rune r, const bool use_utf8) :
		m_rune{r},
		m_is_control{isControlChar(r)} {

	// ascii case: keep single byte width and encoding length
	if (r <= 0x7f || !use_utf8) {
		m_encoded[0] = r;
		return;
	}

	// unicode case

	// for non-control unicode characters check the display width
	if (!m_is_control) {
		// on error stick to a width of 1
		if (int w = ::wcwidth(r); w != -1) {
			m_width = w;
		}
	}

	m_enc_len = utf8::encode(r, m_encoded);
}

} // end ns
