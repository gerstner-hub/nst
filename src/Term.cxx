// C
#include <string.h>

// nst
#include "Term.hxx"
#include "st.h"

Term::Term(int _cols, int _rows) {
	resize(_cols, _rows);
	reset();
}

void Term::reset(void)
{
	c = (nst::TCursor){.attr = {
		.mode = ATTR_NULL,
		.fg = defaultfg,
		.bg = defaultbg
	}, .x = 0, .y = 0, .state = CURSOR_DEFAULT};

	memset(tabs, 0, col * sizeof(*tabs));
	for (size_t i = TABSPACES; (int)i < col; i += TABSPACES)
		tabs[i] = 1;
	top = 0;
	bot = row - 1;
	mode = MODE_WRAP|MODE_UTF8;
	memset(trantbl, CS_USA, sizeof(trantbl));
	charset = 0;

	for (size_t i = 0; i < 2; i++) {
		moveTo(0, 0);
		cursorControl(nst::CursorControl::SAVE);
		clearRegion(0, 0, col-1, row-1);
		swapScreen();
	}
}

void Term::setDirty(int p_top, int p_bot)
{
	LIMIT(top, 0, row-1);
	LIMIT(bot, 0, row-1);

	if (!dirty)
		return;

	for (int i = p_top; i <= p_bot; i++)
		dirty[i] = 1;
}

void Term::resize(int new_cols, int new_rows)
{
	int i;
	const int minrow = std::min(new_rows, row);
	const int mincol = std::min(new_cols, col);

	if (new_cols < 1 || new_rows < 1) {
		fprintf(stderr,
		        "tresize: error resizing to %dx%d\n", new_cols, new_rows);
		return;
	}

	/*
	 * slide screen to keep cursor where we expect it -
	 * tscrollup would work here, but we can optimize to
	 * memmove because we're freeing the earlier lines
	 */
	for (i = 0; i <= c.y - new_rows; i++) {
		free(line[i]);
		free(alt[i]);
	}
	/* ensure that both src and dst are not NULL */
	if (i > 0) {
		memmove(line, line + i, new_rows * sizeof(nst::Line));
		memmove(alt, alt + i, new_rows * sizeof(nst::Line));
	}
	for (i += new_rows; i < row; i++) {
		free(line[i]);
		free(alt[i]);
	}

	/* resize to new height */
	line = (nst::Glyph**)xrealloc(line, new_rows * sizeof(nst::Line));
	alt  = (nst::Glyph**)xrealloc(alt,  new_rows * sizeof(nst::Line));
	dirty = (int*)xrealloc(dirty, new_rows * sizeof(*dirty));
	tabs = (int*)xrealloc(tabs, new_cols * sizeof(*tabs));

	/* resize each row to new width, zero-pad if needed */
	for (i = 0; i < minrow; i++) {
		line[i] = (nst::Line)xrealloc(line[i], new_cols * sizeof(nst::Glyph));
		alt[i]  = (nst::Line)xrealloc(alt[i],  new_cols * sizeof(nst::Glyph));
	}

	/* allocate any new rows */
	for (/* i = minrow */; i < new_rows; i++) {
		line[i] = (nst::Line)xmalloc(new_cols * sizeof(nst::Glyph));
		alt[i] = (nst::Line)xmalloc(new_cols * sizeof(nst::Glyph));
	}
	if (new_cols > col) {
		int *bp = tabs + col;

		memset(bp, 0, sizeof(*tabs) * (new_cols - col));
		while (--bp > tabs && !*bp)
			/* nothing */ ;
		for (bp += TABSPACES; bp < tabs + new_cols; bp += TABSPACES)
			*bp = 1;
	}
	/* update terminal size */
	col = new_cols;
	row = new_rows;
	/* reset scrolling region */
	setScroll(0, new_rows-1);
	/* make use of the LIMIT in moveTo */
	moveTo(c.x, c.y);
	/* Clearing both screens (it makes dirty all lines) */
	nst::TCursor saved_c = c;
	for (i = 0; i < 2; i++) {
		if (mincol < new_cols && 0 < minrow) {
			clearRegion(mincol, 0, new_cols - 1, minrow - 1);
		}
		if (0 < new_cols && minrow < new_rows) {
			clearRegion(0, minrow, new_cols - 1, new_rows - 1);
		}
		swapScreen();
		cursorControl(nst::CursorControl::LOAD);
	}
	c = saved_c;
}

void Term::clearRegion(int x1, int y1, int x2, int y2)
{
	int x, y, temp;
	nst::Glyph *gp;

	if (x1 > x2)
		temp = x1, x1 = x2, x2 = temp;
	if (y1 > y2)
		temp = y1, y1 = y2, y2 = temp;

	LIMIT(x1, 0, col-1);
	LIMIT(x2, 0, col-1);
	LIMIT(y1, 0, row-1);
	LIMIT(y2, 0, row-1);

	for (y = y1; y <= y2; y++) {
		dirty[y] = 1;
		for (x = x1; x <= x2; x++) {
			gp = &line[y][x];
			if (selected(x, y))
				selclear();
			gp->fg = c.attr.fg;
			gp->bg = c.attr.bg;
			gp->mode = 0;
			gp->u = ' ';
		}
	}
}

void Term::setScroll(int t, int b)
{
	LIMIT(t, 0, row-1);
	LIMIT(b, 0, row-1);
	if (t > b) {
		std::swap(t, b);
	}
	top = t;
	bot = b;
}

void Term::moveTo(int x, int y)
{
	int miny, maxy;

	if (c.state & CURSOR_ORIGIN) {
		miny = top;
		maxy = bot;
	} else {
		miny = 0;
		maxy = row - 1;
	}
	c.state &= ~CURSOR_WRAPNEXT;
	c.x = LIMIT(x, 0, col-1);
	c.y = LIMIT(y, miny, maxy);
}

/* for absolute user moves, when decom is set */
void Term::moveAbsTo(int x, int y)
{
	moveTo(x, y + ((c.state & CURSOR_ORIGIN) ? top: 0));
}

void Term::swapScreen()
{
	std::swap(line, alt);
	mode ^= MODE_ALTSCREEN;
	setAllDirty();
}

void Term::cursorControl(const nst::CursorControl &ctrl)
{
	static nst::TCursor cached[2];
	auto &cursor = isSet(MODE_ALTSCREEN) ? cached[1] : cached[0];

	if (ctrl == nst::CursorControl::SAVE) {
		cursor = c;
	} else if (ctrl == nst::CursorControl::LOAD) {
		c = cursor;
		moveTo(cursor.x, cursor.y);
	}
}

int Term::getLineLen(int y) const
{
	auto i = col;

	if (line[y][i - 1].mode & ATTR_WRAP)
		return i;

	while (i > 0 && line[y][i - 1].u == ' ')
		--i;

	return i;
}

void Term::putTab(int n)
{
	auto x = c.x;

	if (n > 0) {
		while (x < col && n--)
			for (++x; x < col && !tabs[x]; ++x)
				/* nothing */ ;
	} else if (n < 0) {
		while (x > 0 && n++)
			for (--x; x > 0 && !tabs[x]; --x)
				/* nothing */ ;
	}
	c.x = LIMIT(x, 0, col-1);
}

void Term::putNewline(bool first_col)
{
	auto y = c.y;

	if (y == bot) {
		scrollUp(top, 1);
	} else {
		y++;
	}
	moveTo(first_col ? 0 : c.x, y);
}

void Term::deleteChar(int n)
{
	LIMIT(n, 0, col - c.x);

	const int dst = c.x;
	const int src = c.x + n;
	const int size = col - src;
	nst::Glyph *l = line[c.y];

	memmove(&l[dst], &l[src], size * sizeof(nst::Glyph));
	clearRegion(col-n, c.y, col-1, c.y);
}

void Term::deleteLine(int n)
{
	if (BETWEEN(c.y, top, bot))
		scrollUp(c.y, n);
}

void Term::insertBlank(int n)
{
	LIMIT(n, 0, col - c.x);

	const int dst = c.x + n;
	const int src = c.x;
	const int size = col - dst;
	nst::Glyph *l = line[c.y];

	memmove(&l[dst], &l[src], size * sizeof(nst::Glyph));
	clearRegion(src, c.y, dst - 1, c.y);
}

void Term::insertBlankLine(int n)
{
	if (BETWEEN(c.y, top, bot))
		scrollDown(c.y, n);
}
