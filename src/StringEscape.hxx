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
#include "Glyph.hxx"

namespace nst {

class Nst;

/// Handles STR (non CSI) escape sequences
/**
 * STR escape sequences follow the following model:
 *
 * ESC type [[ [<priv>] <arg> [;]] <mode>] ESC '\'
 *
 * This mostly implements OSC commands known from XTerm. The final ESC '\' is
 * the ST "string terminator", consisting of two bytes.
 * Alternatively an OSC command can also be terminated by a single BEL control
 * character.
 *
 * XTerm also calls this "string mode" in which single byte controls should
 * still be accepted. See Term::preProcessChar() for this complex logic.
 *
 * This old discussion has a couple of pointers about how this works:
 *
 * https://misc.openbsd.narkive.com/EV7vcwoY/wscons-ansi-terminal-screw-you-mode
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
	/// starts a new string escape sequence of the given type
	void reset(const Type &type);
	/// adds input characters to the current sequence
	void add(const std::string_view &s);
	/// processes a completed escape sequence, 
	void process();

	bool isTerminator(const RuneInfo &ri) const {
		switch(ri.asChar()) {
			case '\a':
			case '\030':
			case '\032':
			case '\033':
				return true;
			default:
				return ri.isControlC1();
		}
	}

protected: // functions

	/// prints the current escape status to stderr
	void dump(const std::string_view &prefix) const;
	/// parses escape sequence arguments from m_str into m_args
	void parseArgs();
	/// process an OSC (Operating System Command)
	bool processOSC();
	void setIconTitle(const char *s);
	void setTitle(const char *s);
	/// reports back the current color mapping for color index
	void oscColorResponse(int index, int code);

protected: // data

	std::string m_str; /// the escape sequence collected so far
	std::vector<std::string_view> m_args; /// views into m_str that make up the arguments
	Type m_esc_type = Type::NONE; // the active escape type being parsed
	Nst &m_nst;
};

} // end ns

#endif // inc. guard
