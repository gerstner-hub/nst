#ifndef NST_ST_H
#define NST_ST_H

// libc
#include <stdint.h>

// stdlib
#include <cstring>
#include <algorithm>

#include "types.hxx"

void die(const char *, ...);

void printscreen(const Arg *);
void printsel(const Arg *);
void toggleprinter(const Arg *);

template <typename T>
T* renew(T *oldptr, size_t oldsize, size_t newsize) {
	T* ret = new T[newsize];
	std::memcpy(ret, oldptr, sizeof(T) * std::min(oldsize, newsize));

	delete[] oldptr;
	return ret;
}

template <typename T>
void setDefault(T &v, const T &def) {
	if (!v)
		v = def;
}

#endif // inc. guard
