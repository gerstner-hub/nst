#ifndef NST_TYPES_HXX
#define NST_TYPES_HXX

#include <stdint.h>

typedef unsigned char uchar;
typedef unsigned int uint;
typedef unsigned long ulong;
typedef unsigned short ushort;

enum charset {
	CS_GRAPHIC0,
	CS_GRAPHIC1,
	CS_UK,
	CS_USA,
	CS_MULTI,
	CS_GER,
	CS_FIN
};

typedef union {
	int i;
	uint ui;
	float f;
	const void *v;
	const char *s;
} Arg;

#endif // inc. guard
