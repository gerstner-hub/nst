#ifndef NST_ST_H
#define NST_ST_H

// libc
#include <stdint.h>

// stdlib
#include <cstring>
#include <algorithm>

#include "types.hxx"

void die(const char *, ...);
void redraw(void);
void draw(void);

void printscreen(const Arg *);
void printsel(const Arg *);
void toggleprinter(const Arg *);

int twrite(const char *, int, int);
void csidump(void);

int xgetcolor(size_t x, unsigned char *r, unsigned char *g, unsigned char *b);

/* nst_config.h globals */
extern int allowaltscreen;

template <typename T>
T* renew(T *oldptr, size_t oldsize, size_t newsize) {
	T* ret = new T[newsize];
	std::memcpy(ret, oldptr, sizeof(T) * std::min(oldsize, newsize));

	delete[] oldptr;
	return ret;
}

#endif // inc. guard
