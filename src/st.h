#ifndef NST_ST_H
#define NST_ST_H

// libc
#include <stdint.h>
#include <sys/types.h>

// stdlib
#include <vector>
#include <string>

#include "types.hxx"
#include "Term.hxx"

/* macros */
#define MIN(a, b)		((a) < (b) ? (a) : (b))
#define MAX(a, b)		((a) < (b) ? (b) : (a))
#define LEN(a)			(sizeof(a) / sizeof(a)[0])
#define BETWEEN(x, a, b)	((a) <= (x) && (x) <= (b))
#define DIVCEIL(n, d)		(((n) + ((d) - 1)) / (d))
#define DEFAULT(a, b)		(a) = (a) ? (a) : (b)
#define LIMIT(x, a, b)		(x) = (x) < (a) ? (a) : (x) > (b) ? (b) : (x)
#define ATTRCMP(a, b)		((a).mode != (b).mode || (a).fg != (b).fg || \
				(a).bg != (b).bg)
#define TIMEDIFF(t1, t2)	((t1.tv_sec-t2.tv_sec)*1000 + \
				(t1.tv_nsec-t2.tv_nsec)/1E6)
#define MODBIT(x, set, bit)	((set) ? ((x) |= (bit)) : ((x) &= ~(bit)))

#define TRUECOLOR(r,g,b)	(1 << 24 | (r) << 16 | (g) << 8 | (b))
#define IS_TRUECOL(x)		(1 << 24 & (x))

typedef unsigned char uchar;
typedef unsigned int uint;
typedef unsigned long ulong;
typedef unsigned short ushort;

typedef union {
	int i;
	uint ui;
	float f;
	const void *v;
	const char *s;
} Arg;

void die(const char *, ...);
void redraw(void);
void draw(void);

void printscreen(const Arg *);
void printsel(const Arg *);
void sendbreak(const Arg *);
void toggleprinter(const Arg *);

int tattrset(int);
void tsetdirtattr(int);
void ttyhangup(void);
int ttynew(const char *, const char *, const char *, const std::vector<std::string>*);
size_t ttyread(void);
void ttyresize(int, int);
void ttywrite(const char *, size_t, int);

void resettitle(void);

void selclear(void);
void selinit(void);
void selstart(int, int, int);
void selextend(int, int, int, int);
int selected(int, int);
char *getsel(void);

size_t utf8encode(nst::Rune, char *);

void *xmalloc(size_t);
void *xrealloc(void *, size_t);
char *xstrdup(const char *);
void xfreeglobals();

int xgetcolor(size_t x, unsigned char *r, unsigned char *g, unsigned char *b);

/* config.h globals */
extern const char *utmp;
extern const char *scroll;
extern const char *stty_args;
extern const char *vtiden;
extern const wchar_t *worddelimiters;
extern int allowaltscreen;
extern const int allowwindowops;
extern const char *termname;
extern const unsigned int TABSPACES;
extern const unsigned int defaultfg;
extern const unsigned int defaultbg;
extern const unsigned int defaultcs;
extern unsigned int cols;
extern unsigned int rows;

#endif // inc. guard
