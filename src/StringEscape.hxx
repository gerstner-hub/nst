#pragma once

// C++
#include <string>
#include <string_view>
#include <vector>

// nst
#include "fwd.hxx"

namespace nst {

/// Handles STR (non CSI) escape sequences.
/**
 * STR escape sequences follow this model:
 *
 *     ESC type [[ [<priv>] <arg> [;]] <mode>] ESC '\'
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
		NONE      = '\0',
		OSC       = ']', // operating system command
		DCS       = 'P', // device control string
		APC       = '_', // application specific program command
		PM        = '^', // privacy message
		SET_TITLE = 'k'  // old title set compatibility
	};

public: // functions

	explicit StringEscape(Nst &nst);
	/// Starts a new string escape sequence of the given type.
	void reset(const Type type);
	/// Adds input characters to the current sequence.
	void add(const std::string_view s);
	/// Processes a completed escape sequence.
	void process();

	/// Returns whether the given rune is a valid StringEscape terminator.
	bool isTerminator(const RuneInfo &ri) const;

protected: // functions

	/// Prints the current escape status to stderr.
	void dump(const std::string_view prefix) const;
	/// Parses escape sequence arguments from m_str into m_args.
	void parseArgs();
	/// Process an OSC (Operating System Command).
	bool processOSC();
	void setIconTitle(const char *s);
	void setTitle(const char *s);
	/// Reports back to the TTY the current color mapping for color index.
	void oscColorResponse(const ColorIndex idx, const int code);

protected: // data

	std::string m_str; /// the escape sequence collected so far.
	std::vector<std::string_view> m_args; /// views into m_str that make up the arguments.
	Type m_esc_type = Type::NONE; // the active escape type being parsed.
	Nst &m_nst;
};

} // end ns
