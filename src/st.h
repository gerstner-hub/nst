#ifndef NST_ST_H
#define NST_ST_H

// libc
#include <stdint.h>
#include <sys/types.h>

// stdlib
#include <vector>
#include <string>

#include "types.hxx"
#include "Glyph.hxx"
#include "macros.hxx"

void die(const char *, ...);
void redraw(void);
void draw(void);

void printscreen(const Arg *);
void printsel(const Arg *);
void sendbreak(const Arg *);
void toggleprinter(const Arg *);

int twrite(const char *, int, int);
void csidump(void);

void *xmalloc(size_t);
void *xrealloc(void *, size_t);
char *xstrdup(const char *);
void xfreeglobals();

int xgetcolor(size_t x, unsigned char *r, unsigned char *g, unsigned char *b);

/* config.h globals */
extern int allowaltscreen;
extern unsigned int cols;
extern unsigned int rows;

#endif // inc. guard
