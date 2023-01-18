#ifndef NST_CSIESCAPE_HXX
#define NST_CSIESCAPE_HXX

// libc
#include <stddef.h>

// stdlib
#include <string>
#include <vector>

// cosmos
#include "cosmos/algs.hxx"

// nst
#include "codecs.hxx"
#include "helper.hxx"

namespace nst {

class Nst;
struct StringEscape;

/// Handles CSI escape sequences
/**
 * CSI (Control Sequence Introducer) struct follow the following model:
 *
 * ESC '[' [[ [<priv>] <arg> [;]] <mode> [<mode>]]
 *
 * This class parses such sequences and triggers actions that result from the
 * sequences.
 **/
struct CSIEscape {
public: // functions

	CSIEscape(Nst &nst, StringEscape &str_escape);

	void handle(void);
	void parse(void);
	/*
	 * returns 1 when the sequence is finished and it hasn't to read
	 * more characters for this sequence, otherwise 0
	 */
	int eschandle(unsigned char);

	void dump(const char *prefix) const;

	/// adds the given character to the sequence, returns whether the sequence is complete
	bool add(char ch) {
		m_str.push_back(ch);
		// signal complete either if the maximum sequence length has
		// been reached or a terminating character appears
		return m_str.length() >= MAX_STR_SIZE || cosmos::in_range(ch, 0x40, 0x7E);
	}

	void reset() {
		m_priv = false;
		m_mode[0] = m_mode[1] = 0;
		m_args.clear();
		m_str.clear();
	}

protected: // functions

	//! makes sure the given argument index exists in m_args and if zero
	//! sets it to defval
	void ensureArg(size_t index, int defval) {
		while (m_args.size() < (index + 1))
			m_args.push_back(0);

		setDefault(m_args[index], defval);
	}

protected: // data

	std::string m_str; // the escape sequence collected so far
	bool m_priv = false;
	char m_mode[2] = {0};
	std::vector<int> m_args;
	static constexpr size_t MAX_STR_SIZE = 128 * utf8::UTF_SIZE;
	Nst &m_nst;
	StringEscape &m_str_escape;

};

} // end ns

#endif // inc. guard
