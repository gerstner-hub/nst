#ifndef NST_MACROS_HXX
#define NST_MACROS_HXX

// cosmos
#include "cosmos/algs.hxx"

// libc
#include <wchar.h>

// nst
#include "codecs.hxx"

/* macros */
#define LEN(a)			(sizeof(a) / sizeof(a)[0])
#define DIVCEIL(n, d)		(((n) + ((d) - 1)) / (d))
#define DEFAULT(a, b)		(a) = (a) ? (a) : (b)
#define ATTRCMP(a, b)		((a).mode != (b).mode || (a).fg != (b).fg || \
				(a).bg != (b).bg)
#define TIMEDIFF(t1, t2)	((t1.tv_sec-t2.tv_sec)*1000 + \
				(t1.tv_nsec-t2.tv_nsec)/1E6)
#define MODBIT(x, set, bit)	((set) ? ((x) |= (bit)) : ((x) &= ~(bit)))

#define IS_TRUECOL(x)		(1 << 24 & (x))

#define ISCONTROLC0(c)		((c < 0x1f) || (c) == 0x7f)
#define ISCONTROLC1(c)		(cosmos::in_range(c, 0x80, 0x9f))
#define ISCONTROL(c)		(ISCONTROLC0(c) || ISCONTROLC1(c))
#define ISDELIM(u)		(u && wcschr(nst::config::WORDDELIMITERS, u))

/* Arbitrary sizes */
#define ESC_BUF_SIZ   (128*nst::utf8::UTF_SIZE)
#define ESC_ARG_SIZ   16
#define STR_BUF_SIZ   ESC_BUF_SIZ
#define STR_ARG_SIZ   ESC_ARG_SIZ

#endif // inc. guard
