#ifndef NST_TERM_HXX
#define NST_TERM_HXX

#include "types.hxx"

/// Internal representation of the screen
class Term {
public: // data
	int row = 0;            /* nb row */
	int col = 0;            /* nb col */
	nst::Line *line = nullptr;   /* screen */
	nst::Line *alt = nullptr;    /* alternate screen */
	int *dirty = nullptr;   /* dirtyness of lines */
	nst::TCursor c;              /* cursor */
	int ocx = 0;            /* old cursor col */
	int ocy = 0;            /* old cursor row */
	int top = 0;            /* top    scroll limit */
	int bot = 0;            /* bottom scroll limit */
	int mode = 0;           /* terminal mode flags */
	int esc = 0;            /* escape state flags */
	char trantbl[4] = {0};  /* charset table translation */
	int charset = 0;        /* current charset */
	int icharset = 0;       /* selected charset for sequence */
	int *tabs = nullptr;

	nst::Rune lastc = 0;         /* last printed char outside of sequence, 0 if control */

public: // functions

	Term();

	void resize(int cols, int rows);

	void reset();
};

extern Term term;

#endif // inc. guard
