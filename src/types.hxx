#ifndef NST_TYPES_HXX
#define NST_TYPES_HXX

namespace nst {

typedef uint_least32_t Rune;

typedef struct {
	Rune u = 0;       /* character code */
	unsigned short mode = 0;  /* attribute flags */
	uint32_t fg = 0;  /* foreground  */
	uint32_t bg = 0;  /* background  */
} Glyph;

typedef Glyph *Line;

typedef struct {
	Glyph attr; /* current char attributes */
	int x = 0;
	int y = 0;
	char state = 0;
} TCursor;

} // end ns

#endif // inc. guard
