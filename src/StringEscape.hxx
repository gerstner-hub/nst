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
public: // data
	static constexpr int STR_ARG_SIZE = 16;
	char type = 0;         /* ESC type ... */
	char *buf = nullptr;   /* allocated raw string */
	size_t siz = 0;        /* allocation size */
	size_t len = 0;        /* raw string length */
	char *args[STR_ARG_SIZE] = {nullptr};
	int narg = 0;          /* nb of args */
public: // functions

	~STREscape() {
		delete[] buf;
	}

	void resize(size_t alloc) {
		buf = renew(buf, siz, alloc);
		siz = alloc;
	}

	void reset();
	void handle();

protected: // functions

	/// prints the current escape status to stderr
	void dump(const char *prefix) const;
	void parse();
};

} // end ns

extern nst::STREscape strescseq;

#endif // inc. guard
