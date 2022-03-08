#ifndef NST_STRINGESCAPE_HXX
#define NST_STRINGESCAPE_HXX

// libc
#include <stddef.h>

// nst
#include "codecs.hxx"
#include "st.h"

namespace nst {

/* STR Escape sequence structs */
/* ESC type [[ [<priv>] <arg> [;]] <mode>] ESC '\' */
struct STREscape {
protected: // data

	static constexpr int MAX_STR_ARGS = 16;
	size_t m_alloc_size = 0;
	size_t m_used_len = 0;

	char *m_buf = nullptr; /* allocated raw string */
	char *m_args[MAX_STR_ARGS] = {nullptr};
	char m_esc_type = 0;
	size_t m_num_args = 0;

public: // functions

	~STREscape() {
		delete[] m_buf;
	}

	void reset(const char type);
	void add(const char *ch, size_t len);
	void handle();

protected: // functions

	void resize(size_t alloc) {
		m_buf = renew(m_buf, m_alloc_size, alloc);
		m_alloc_size = alloc;
	}

	/// prints the current escape status to stderr
	void dump(const char *prefix) const;
	void parse();
};

} // end ns

extern nst::STREscape strescseq;

#endif // inc. guard
