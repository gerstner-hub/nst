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
#include "Selection.hxx"
#include "macros.hxx"

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

void init_term(int, int);
void selstart(int, int, int);
void selextend(int, int, int, int);
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
