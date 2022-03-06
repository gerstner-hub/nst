#ifndef NST_CSIESCAPE_HXX
#define NST_CSIESCAPE_HXX

// libc
#include <stddef.h>

// nst
#include "macros.hxx"
#include "codecs.hxx"

namespace nst {

/* CSI (Control Sequence Introducer) Escape sequence structs */
/* ESC '[' [[ [<priv>] <arg> [;]] <mode> [<mode>]] */
struct CSIEscape {
public: // data
	static constexpr size_t ESC_BUF_SIZE = 128 * utf8::UTF_SIZE;
	static constexpr size_t ESC_ARG_SIZE = 16;

	char buf[ESC_BUF_SIZE] = {0}; /* raw string */
	size_t len = 0;            /* raw string length */
	char priv = 0;
	int arg[ESC_ARG_SIZE] = {0};
	int narg = 0;              /* nb of args */
	char mode[2] = {0};

public: // functions

	void handle(void);
	void parse(void);
	/*
	 * returns 1 when the sequence is finished and it hasn't to read
	 * more characters for this sequence, otherwise 0
	 */
	int eschandle(unsigned char);

	void dump(const char *prefix);
};

} // end ns

extern nst::CSIEscape csiescseq;

#endif // inc. guard
