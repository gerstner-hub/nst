#pragma once

// libc
#include <stddef.h>

// C++
#include <string>
#include <string_view>

// cosmos
#include "cosmos/algs.hxx"

// nst
#include "types.hxx"

/**
 * @file
 *
 * This header contains helper functions and types for dealing with character
 * encodings.
 **/

namespace nst {

namespace utf8 {

	constexpr size_t UTF_SIZE = 4;

	/// Decodes a single UTF8 character from encoded and stores the result in \c u
	/**
	 * \return The number of bytes processed from \c encoded
	 **/
	size_t decode(const std::string_view encoded, Rune &u);
	/// Encodes the given rune into utf8 and returns the result in \c out
	/**
	 * \return the number of bytes placed in \c out
	 **/
	size_t encode(Rune rune, char out[UTF_SIZE]);
	/// Encodes the given rune into utf8 and appends the result in \c s
	void encode(Rune rune, std::string &s);

} // end utf8

namespace base64 {

	/// decodes a base64 encoded string and returns a string containing the decoded result
	std::string decode(const std::string_view s);

} // end base64

/// Helper type for processing Runes related to UTF8 encoding and control chars.
class RuneInfo {
public: // functions
	RuneInfo(Rune r, const bool use_utf8);

	Rune rune() const { return m_rune; }
	int width() const { return m_width; }

	std::string_view encoded() const {
		return {&m_encoded[0], m_enc_len};
	}

	bool isWide() const { return m_width == 2; }
	bool isControlChar() const { return m_is_control; }
	bool isControlC0() const { return isControlC0(m_rune); }
	bool isControlC1() const { return isControlC1(m_rune); }

	unsigned char asChar() const { return static_cast<unsigned char>(m_rune); }

	/// checks whether the given rune is an ASCII 7 bit control code (C0 class)
	static bool isControlC0(const nst::Rune r) {
		return r < 0x1f || r == 0x7f;
	}

	/// checks whether the given rune is an extended 8 bit control code (C1 class)
	static bool isControlC1(const nst::Rune r) {
		return cosmos::in_range(r, 0x80, 0x9f);
	}

	static bool isControlChar(const nst::Rune r) {
		return isControlC0(r) || isControlC1(r);
	}

protected: // data

	Rune m_rune = 0;
	const bool m_is_control = false;
	int m_width = 1;
	size_t m_enc_len = 1; /// valid bytes in m_encoded
	char m_encoded[utf8::UTF_SIZE];
};

} // end ns
