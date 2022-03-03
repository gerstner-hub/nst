#ifndef NST_ST_H
#define NST_ST_H

// libc
#include <stdint.h>
#include <sys/types.h>

// stdlib
#include <vector>
#include <string>

#include "types.hxx"
#include "macros.hxx"

void die(const char *, ...);
void redraw(void);
void draw(void);

void printscreen(const Arg *);
void printsel(const Arg *);
void sendbreak(const Arg *);
void toggleprinter(const Arg *);

int tattrset(int);
void tsetdirtattr(int);
int twrite(const char *, int, int);

char *getsel(void);

size_t utf8encode(nst::Rune, char *);

void *xmalloc(size_t);
void *xrealloc(void *, size_t);
char *xstrdup(const char *);
void xfreeglobals();

int xgetcolor(size_t x, unsigned char *r, unsigned char *g, unsigned char *b);

/* config.h globals */
extern const char *vtiden;
extern int allowaltscreen;
extern const int allowwindowops;
extern const unsigned int TABSPACES;
extern const unsigned int defaultfg;
extern const unsigned int defaultbg;
extern const unsigned int defaultcs;
extern unsigned int cols;
extern unsigned int rows;

#endif // inc. guard
