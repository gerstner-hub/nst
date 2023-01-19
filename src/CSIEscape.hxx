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

	explicit CSIEscape(Nst &nst);

	void process();
	void parse();

	/// returns true if the sequence is complete
	bool handleEscape(const char ch);

	/// adds the given character to the sequence, returns whether the sequence is complete
	bool addCSI(const char ch) {
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

	/// makes sure the given argument index exists in m_args and if zero sets it to defval
	void ensureArg(size_t index, int defval) {
		while (m_args.size() < (index + 1))
			m_args.push_back(0);

		setDefault(m_args[index], defval);
	}

	void dump(const char *prefix) const;

protected: // data

	std::string m_str; // the escape sequence collected so far
	bool m_priv = false;
	char m_mode[2] = {0};
	std::vector<int> m_args;
	static constexpr size_t MAX_STR_SIZE = 128 * utf8::UTF_SIZE;
	Nst &m_nst;

};

} // end ns

#endif // inc. guard
