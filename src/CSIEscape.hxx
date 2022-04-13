#ifndef NST_CSIESCAPE_HXX
#define NST_CSIESCAPE_HXX

// libc
#include <stddef.h>

// stdlib
#include <string>
#include <vector>

// nst
#include "codecs.hxx"
#include "helper.hxx"

namespace nst {

/* CSI (Control Sequence Introducer) Escape sequence structs */
/* ESC '[' [[ [<priv>] <arg> [;]] <mode> [<mode>]] */
struct CSIEscape {
protected: // data

	bool m_priv = false;
	char m_mode[2] = {0};
	std::vector<int> m_args;
	std::string m_str;
	static constexpr size_t MAX_STR_SIZE = 128 * utf8::UTF_SIZE;

protected: // functions

	//! makes sure the given argument index exists in m_args and if zero
	//! sets it to defval
	void ensureArg(size_t index, int defval) {
		while (m_args.size() < (index + 1))
			m_args.push_back(0);

		setDefault(m_args[index], defval);
	}

public: // functions

	CSIEscape();

	void handle(void);
	void parse(void);
	/*
	 * returns 1 when the sequence is finished and it hasn't to read
	 * more characters for this sequence, otherwise 0
	 */
	int eschandle(unsigned char);

	void dump(const char *prefix) const;

	//! adds the given character to the sequence, returns whether the
	//! maximum sequence length has been reached
	bool add(char ch) {
		m_str.push_back(ch);
		return m_str.length() >= MAX_STR_SIZE;
	}
};

} // end ns

extern nst::CSIEscape csiescseq;

#endif // inc. guard
