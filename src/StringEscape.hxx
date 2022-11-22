#ifndef NST_STRINGESCAPE_HXX
#define NST_STRINGESCAPE_HXX

// libc
#include <stddef.h>

// stdlib
#include <string>
#include <vector>

// nst
#include "codecs.hxx"

namespace nst {

class Term;

/* STR Escape sequence structs */
/* ESC type [[ [<priv>] <arg> [;]] <mode>] ESC '\' */
struct STREscape {
protected: // data

	std::string m_str; /* raw string */
	std::vector<const char*> m_args;
	char m_esc_type = 0;
	Term &m_term;

public: // functions

	STREscape(Term &term) : m_term(term) {}
	void reset(const char type);
	void add(const char *ch, size_t len);
	void handle();

protected: // functions

	/// prints the current escape status to stderr
	void dump(const char *prefix) const;
	void parse();
};

} // end ns

#endif // inc. guard
