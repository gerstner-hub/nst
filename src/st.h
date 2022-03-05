#ifndef NST_ST_H
#define NST_ST_H

// libc
#include <stdint.h>
#include <sys/types.h>

// stdlib
#include <vector>
#include <cstring>

#include "types.hxx"
#include "Glyph.hxx"
#include "macros.hxx"

void die(const char *, ...);
void redraw(void);
void draw(void);

void printscreen(const Arg *);
void printsel(const Arg *);
void toggleprinter(const Arg *);

int twrite(const char *, int, int);
void csidump(void);

int xgetcolor(size_t x, unsigned char *r, unsigned char *g, unsigned char *b);

/* config.h globals */
extern int allowaltscreen;
extern unsigned int cols;
extern unsigned int rows;

template <typename T>
T* renew(T *oldptr, size_t oldsize, size_t newsize) {
	T* ret = new T[newsize];
	std::memcpy(ret, oldptr, sizeof(T) * std::min(oldsize, newsize));

	delete[] oldptr;
	return ret;
}

#endif // inc. guard
