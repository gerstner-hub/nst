#ifndef NST_TYPES_HXX
#define NST_TYPES_HXX

#include <stdint.h>

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

#endif // inc. guard
