#ifndef NST_STRINGESCAPE_HXX
#define NST_STRINGESCAPE_HXX

// libc
#include <stddef.h>

// stdlib
#include <string>
#include <string_view>
#include <vector>

// nst
#include "codecs.hxx"

namespace nst {

class Nst;

/// Handles STR escape sequences
/**
 * STR escape sequences follow the following model:
 *
 * ESC type [[ [<priv>] <arg> [;]] <mode>] ESC '\'
 *
 * This mostly implements OSC commands known from XTerm. Unimplemented
 * commands are ignored.
 **/
struct StringEscape {
public: // types

	enum class Type : char {
		NONE = '\0',
		OSC = ']', // operating system command
		DCS = 'P', // device control string
		APC = '_', // application specific program command
		PM = '^',  // privacy message
		SET_TITLE = 'k' // old title set compatibility
	};

public: // functions

	explicit StringEscape(Nst &nst);
	void reset(const Type &type);
	void add(const char *ch, size_t len);
	void add(const std::string_view &s) {
		add(s.data(), s.size());
	}
	void handle();

protected: // functions

	/// prints the current escape status to stderr
	void dump(const char *prefix) const;
	void parse();
	void setIconTitle(const char *s);
	void setTitle(const char *s);
	void osc4ColorResponse(int num);
	void oscColorResponse(int index, int num);

protected: // data

	std::string m_str; /* raw string */
	std::vector<const char*> m_args;
	Type m_esc_type = Type::NONE; // the active escape type being parsed
	Nst &m_nst;

};

} // end ns

#endif // inc. guard
