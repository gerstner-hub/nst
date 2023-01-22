#ifndef NST_CODECS_HXX
#define NST_CODECS_HXX

// libc
#include <stddef.h>

// C++
#include <string>
#include <string_view>

// cosmos
#include "cosmos/algs.hxx"

// nst
#include "Glyph.hxx"

namespace nst {

namespace utf8 {

constexpr size_t UTF_SIZE = 4;

size_t decode(const char *, Rune *, size_t);
size_t encode(Rune u, char *c);
void encode(Rune u, std::string &s);
size_t validate(Rune *, size_t);

}

namespace base64 {

/// decodes a base64 encoded string and returns a string containing the decoded result
std::string decode(const std::string_view &s);

}

/// helper type for processing Runes related to UTF8 encoding and control chars
class RuneInfo {
public: // functions
	RuneInfo(Rune r, const bool use_utf8);

	Rune rune() const { return m_rune; }
	int width() const { return m_width; }

	std::string_view getEncoded() const {
		return {&m_encoded[0], m_enc_len};
	}

	bool isWide() const { return m_width == 2; }
	bool isControlChar() const { return m_is_control; }
	bool isControlC0() const { return isControlC0(m_rune); }
	bool isControlC1() const { return isControlC1(m_rune); }

	unsigned char asChar() const { return static_cast<unsigned char>(m_rune); }

	/// checks whether the given rune is an ASCII 7 bit control character (C0 class)
	static bool isControlC0(const nst::Rune &r) {
		return r < 0x1f || r == 0x7f;
	}

	/// checks whether the given rune is an extended 8 bit control code (C1 class)
	static bool isControlC1(const nst::Rune &r) {
		return cosmos::in_range(r, 0x80, 0x9f);
	}

	static bool isControlChar(const nst::Rune &r) {
		return isControlC0(r) || isControlC1(r);
	}

protected: // data

	Rune m_rune = 0;
	bool m_is_control = false;
	int m_width = 1;
	size_t m_enc_len = 1; /// valid bytes in m_encoded
	char m_encoded[utf8::UTF_SIZE];
};

} // end ns

#endif // inc. guard
