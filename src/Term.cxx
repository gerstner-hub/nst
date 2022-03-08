// C
#include <string.h>

// stdlib
#include <algorithm>
#include <iostream>

// cosmos
#include "cosmos/algs.hxx"

// nst
#include "nst_config.h"
#include "CSIEscape.hxx"
#include "Term.hxx"
#include "TTY.hxx"
#include "Selection.hxx"
#include "StringEscape.hxx"
#include "codecs.hxx"
#include "st.h"
#include "win.h"

using cosmos::in_range;

nst::Term term;

namespace nst {

typedef Glyph::Attr Attr;

Term::Term(int _cols, int _rows) {
	m_selection = &g_sel;
	m_tty = &g_tty;
	resize(_cols, _rows);
	reset();
}

void Term::reset(void) {
	c = (TCursor){.attr = {
		.mode = Glyph::AttrBitMask(),
		.fg = config::DEFAULTFG,
		.bg = config::DEFAULTBG
	}, .x = 0, .y = 0, .state = TCursor::StateBitMask()};

	memset(tabs, 0, col * sizeof(*tabs));
	for (size_t i = config::TABSPACES; (int)i < col; i += config::TABSPACES)
		tabs[i] = 1;
	top = 0;
	bot = row - 1;
	mode.set(Mode::WRAP);
	mode.set(Mode::UTF8);
	memset(trantbl, CS_USA, sizeof(trantbl));
	charset = 0;

	for (size_t i = 0; i < 2; i++) {
		moveTo(0, 0);
		cursorControl(TCursor::Control::SAVE);
		clearRegion(0, 0, col-1, row-1);
		swapScreen();
	}
}

void Term::setDirty(int p_top, int p_bot) {
	p_top = std::clamp(p_top, 0, row-1);
	p_bot = std::clamp(p_bot, 0, row-1);

	if (!dirty)
		return;

	for (int i = p_top; i <= p_bot; i++)
		dirty[i] = 1;
}

void Term::resize(int new_cols, int new_rows) {
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
		delete[] line[i];
		delete[] alt[i];
	}
	/* ensure that both src and dst are not NULL */
	if (i > 0) {
		memmove(line, line + i, new_rows * sizeof(Line));
		memmove(alt, alt + i, new_rows * sizeof(Line));
	}
	for (i += new_rows; i < row; i++) {
		delete[] line[i];
		delete[] alt[i];
	}

	/* allocate new memory with new height */
	line = renew(line, row, new_rows);
	alt = renew(alt, row, new_rows);
	dirty = renew(dirty, row, new_rows);
	tabs = renew(tabs, col, new_cols);

	/* resize each row to new width, zero-pad if needed */
	for (i = 0; i < minrow; i++) {
		line[i] = renew(line[i], col, new_cols);
		alt[i] = renew(alt[i], col, new_cols);
	}

	/* allocate any new rows */
	for (i = minrow; i < new_rows; i++) {
		line[i] = new Glyph[new_cols];
		alt[i] = new Glyph[new_cols];
	}
	if (new_cols > col) {
		int *bp = tabs + col;

		memset(bp, 0, sizeof(*tabs) * (new_cols - col));
		while (--bp > tabs && !*bp)
			/* nothing */ ;
		for (bp += config::TABSPACES; bp < tabs + new_cols; bp += config::TABSPACES)
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
	TCursor saved_c = c;
	for (i = 0; i < 2; i++) {
		if (mincol < new_cols && 0 < minrow) {
			clearRegion(mincol, 0, new_cols - 1, minrow - 1);
		}
		if (0 < new_cols && minrow < new_rows) {
			clearRegion(0, minrow, new_cols - 1, new_rows - 1);
		}
		swapScreen();
		cursorControl(TCursor::Control::LOAD);
	}
	c = saved_c;
}

void Term::clearRegion(int x1, int y1, int x2, int y2) {
	if (x1 > x2)
		std::swap(x1, x2);
	if (y1 > y2)
		std::swap(y1, y2);

	x1 = std::clamp(x1, 0, col-1);
	x2 = std::clamp(x2, 0, col-1);
	y1 = std::clamp(y1, 0, row-1);
	y2 = std::clamp(y2, 0, row-1);

	Glyph *gp;

	for (int y = y1; y <= y2; y++) {
		dirty[y] = 1;
		for (int x = x1; x <= x2; x++) {
			gp = &line[y][x];
			if (m_selection->isSelected(x, y))
				m_selection->clear();
			gp->fg = c.attr.fg;
			gp->bg = c.attr.bg;
			gp->mode.reset();
			gp->u = ' ';
		}
	}
}

void Term::setScroll(int t, int b) {
	t = std::clamp(t, 0, row-1);
	b = std::clamp(b, 0, row-1);
	if (t > b) {
		std::swap(t, b);
	}
	top = t;
	bot = b;
}

void Term::moveTo(int x, int y)
{
	int miny, maxy;

	if (c.state.test(TCursor::State::ORIGIN)) {
		miny = top;
		maxy = bot;
	} else {
		miny = 0;
		maxy = row - 1;
	}
	c.state.reset(TCursor::State::WRAPNEXT);
	c.x = std::clamp(x, 0, col-1);
	c.y = std::clamp(y, miny, maxy);
}

/* for absolute user moves, when decom is set */
void Term::moveAbsTo(int x, int y) {
	moveTo(x, y + ((c.state.test(TCursor::State::ORIGIN)) ? top: 0));
}

void Term::swapScreen() {
	std::swap(line, alt);
	mode.flip(Mode::ALTSCREEN);
	setAllDirty();
}

void Term::cursorControl(const TCursor::Control &ctrl)
{
	static TCursor cached[2];
	auto &cursor = mode.test(Mode::ALTSCREEN) ? cached[1] : cached[0];

	if (ctrl == TCursor::Control::SAVE) {
		cursor = c;
	} else if (ctrl == TCursor::Control::LOAD) {
		c = cursor;
		moveTo(cursor.x, cursor.y);
	}
}

int Term::getLineLen(int y) const
{
	auto i = col;

	if (line[y][i - 1].mode.test(Attr::WRAP))
		return i;

	while (i > 0 && line[y][i - 1].u == ' ')
		--i;

	return i;
}

void Term::putTab(int n) {
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
	c.x = std::clamp(x, 0, col-1);
}

void Term::putNewline(bool first_col) {
	auto y = c.y;

	if (y == bot) {
		scrollUp(top, 1);
	} else {
		y++;
	}
	moveTo(first_col ? 0 : c.x, y);
}

void Term::deleteChar(int n) {
	n = std::clamp(n, 0, col - c.x);

	const int dst = c.x;
	const int src = c.x + n;
	const int size = col - src;
	Glyph *l = line[c.y];

	memmove(&l[dst], &l[src], size * sizeof(Glyph));
	clearRegion(col-n, c.y, col-1, c.y);
}

void Term::deleteLine(int n) {
	if (in_range(c.y, top, bot))
		scrollUp(c.y, n);
}

void Term::insertBlank(int n)
{
	n = std::clamp(n, 0, col - c.x);

	const int dst = c.x + n;
	const int src = c.x;
	const int size = col - dst;
	Glyph *l = line[c.y];

	memmove(&l[dst], &l[src], size * sizeof(Glyph));
	clearRegion(src, c.y, dst - 1, c.y);
}

void Term::insertBlankLine(int n)
{
	if (in_range(c.y, top, bot))
		scrollDown(c.y, n);
}

void Term::scrollDown(int orig, int n)
{
	n = std::clamp(n, 0, bot-orig+1);

	setDirty(orig, bot-n);
	clearRegion(0, bot-n+1, col-1, bot);

	for (int i = bot; i >= orig+n; i--) {
		std::swap(line[i], line[i-n]);
	}

	m_selection->scroll(orig, n);
}

void Term::scrollUp(int orig, int n)
{
	n = std::clamp(n, 0, bot-orig+1);

	clearRegion(0, orig, col-1, orig+n-1);
	setDirty(orig+n, bot);

	for (int i = orig; i <= bot-n; i++) {
		std::swap(line[i], line[i+n]);
	}

	m_selection->scroll(orig, -n);
}

void Term::setAttr(const int *attr, size_t len) {
	int32_t idx;

	for (size_t i = 0; i < len; i++) {
		switch (attr[i]) {
		case 0:
			c.attr.mode.reset({
				Attr::BOLD,
				Attr::FAINT,
				Attr::ITALIC,
				Attr::UNDERLINE,
				Attr::BLINK,
				Attr::REVERSE,
				Attr::INVISIBLE,
				Attr::STRUCK
			});
			c.attr.fg = config::DEFAULTFG;
			c.attr.bg = config::DEFAULTBG;
			break;
		case 1:
			c.attr.mode.set(Attr::BOLD);
			break;
		case 2:
			c.attr.mode.set(Attr::FAINT);
			break;
		case 3:
			c.attr.mode.set(Attr::ITALIC);
			break;
		case 4:
			c.attr.mode.set(Attr::UNDERLINE);
			break;
		case 5: /* slow blink */
			/* FALLTHROUGH */
		case 6: /* rapid blink */
			c.attr.mode.set(Attr::BLINK);
			break;
		case 7:
			c.attr.mode.set(Attr::REVERSE);
			break;
		case 8:
			c.attr.mode.set(Attr::INVISIBLE);
			break;
		case 9:
			c.attr.mode.set(Attr::STRUCK);
			break;
		case 22:
			c.attr.mode.reset({Attr::BOLD, Attr::FAINT});
			break;
		case 23:
			c.attr.mode.reset(Attr::ITALIC);
			break;
		case 24:
			c.attr.mode.reset(Attr::UNDERLINE);
			break;
		case 25:
			c.attr.mode.reset(Attr::BLINK);
			break;
		case 27:
			c.attr.mode.reset(Attr::REVERSE);
			break;
		case 28:
			c.attr.mode.reset(Attr::INVISIBLE);
			break;
		case 29:
			c.attr.mode.reset(Attr::STRUCK);
			break;
		case 38:
			if ((idx = defcolor(attr, &i, len)) >= 0)
				c.attr.fg = idx;
			break;
		case 39:
			c.attr.fg = config::DEFAULTFG;
			break;
		case 48:
			if ((idx = defcolor(attr, &i, len)) >= 0)
				c.attr.bg = idx;
			break;
		case 49:
			c.attr.bg = config::DEFAULTBG;
			break;
		default:
			if (in_range(attr[i], 30, 37)) {
				c.attr.fg = attr[i] - 30;
			} else if (in_range(attr[i], 40, 47)) {
				c.attr.bg = attr[i] - 40;
			} else if (in_range(attr[i], 90, 97)) {
				c.attr.fg = attr[i] - 90 + 8;
			} else if (in_range(attr[i], 100, 107)) {
				c.attr.bg = attr[i] - 100 + 8;
			} else {
				std::cerr << "erresc(default): gfx attr " << attr[i] << " unknown\n",
				csiescseq.dump("");
			}
			break;
		}
	}
}

int32_t Term::defcolor(const int *attr, size_t *npar, size_t len) {
	switch (attr[*npar + 1]) {
	case 2: /* direct color in RGB space */ {
		if (*npar + 4 >= len) {
			std::cerr << "erresc(38): Incorrect number of parameters (" << *npar << ")\n";
			break;
		}
		const uint r = attr[*npar + 2];
		const uint g = attr[*npar + 3];
		const uint b = attr[*npar + 4];
		*npar += 4;
		if (r > 255 || g > 255 || b > 255) {
			std::cerr << "erresc: bad rgb color (" << r << "," << g << "," << b << ")" << std::endl;
			break;
		}

		return toTrueColor(r, g, b);
	}
	case 5: /* indexed color */
		if (*npar + 2 >= len) {
			std::cerr << "erresc(38): Incorrect number of parameters (" << *npar << ")" << std::endl;
			break;
		}
		*npar += 2;
		if (!in_range(attr[*npar], 0, 255)) {
			std::cerr << "erresc: bad fgcolor " << attr[*npar] << std::endl;
			break;
		}

		return attr[*npar];
	case 0: /* implementation defined (only foreground) */
	case 1: /* transparent */
	case 3: /* direct color in CMY space */
	case 4: /* direct color in CMYK space */
	default:
		std::cerr << "erresc(38): gfx attr " << attr[*npar] << " unknown" << std::endl;
		break;
	}

	return -1;
}

void Term::setMode(int priv, int set, const int *args, int narg) {
	for (const int *lim = args + narg; args < lim; ++args) {
		if (priv) {
			switch (*args) {
			case 1: /* DECCKM -- Cursor key */
				xsetmode(set, MODE_APPCURSOR);
				break;
			case 5: /* DECSCNM -- Reverse video */
				xsetmode(set, MODE_REVERSE);
				break;
			case 6: /* DECOM -- Origin */
				c.state.set(TCursor::State::ORIGIN, set);
				moveAbsTo(0, 0);
				break;
			case 7: /* DECAWM -- Auto wrap */
				mode.set(Mode::WRAP, set);
				break;
			case 0:  /* Error (IGNORED) */
			case 2:  /* DECANM -- ANSI/VT52 (IGNORED) */
			case 3:  /* DECCOLM -- Column  (IGNORED) */
			case 4:  /* DECSCLM -- Scroll (IGNORED) */
			case 8:  /* DECARM -- Auto repeat (IGNORED) */
			case 18: /* DECPFF -- Printer feed (IGNORED) */
			case 19: /* DECPEX -- Printer extent (IGNORED) */
			case 42: /* DECNRCM -- National characters (IGNORED) */
			case 12: /* att610 -- Start blinking cursor (IGNORED) */
				break;
			case 25: /* DECTCEM -- Text Cursor Enable Mode */
				xsetmode(!set, MODE_HIDE);
				break;
			case 9:    /* X10 mouse compatibility mode */
				xsetpointermotion(0);
				xsetmode(0, MODE_MOUSE);
				xsetmode(set, MODE_MOUSEX10);
				break;
			case 1000: /* 1000: report button press */
				xsetpointermotion(0);
				xsetmode(0, MODE_MOUSE);
				xsetmode(set, MODE_MOUSEBTN);
				break;
			case 1002: /* 1002: report motion on button press */
				xsetpointermotion(0);
				xsetmode(0, MODE_MOUSE);
				xsetmode(set, MODE_MOUSEMOTION);
				break;
			case 1003: /* 1003: enable all mouse motions */
				xsetpointermotion(set);
				xsetmode(0, MODE_MOUSE);
				xsetmode(set, MODE_MOUSEMANY);
				break;
			case 1004: /* 1004: send focus events to tty */
				xsetmode(set, MODE_FOCUS);
				break;
			case 1006: /* 1006: extended reporting mode */
				xsetmode(set, MODE_MOUSESGR);
				break;
			case 1034:
				xsetmode(set, MODE_8BIT);
				break;
			case 1049: /* swap screen & set/restore cursor as xterm */
				if (!allowaltscreen)
					break;
				cursorControl((set) ? TCursor::Control::SAVE : TCursor::Control::LOAD);
				/* FALLTHROUGH */
			case 47: /* swap screen */
			case 1047: {
				if (!allowaltscreen)
					break;
				const auto is_alt = mode.test(Mode::ALTSCREEN);
				if (is_alt) {
					clearRegion(0, 0, col-1, row-1);
				}
				if (set ^ is_alt) /* set is always 1 or 0 */
					swapScreen();
				if (*args != 1049)
					break;
			}
				/* FALLTHROUGH */
			case 1048:
				cursorControl((set) ? TCursor::Control::SAVE : TCursor::Control::LOAD);
				break;
			case 2004: /* 2004: bracketed paste mode */
				xsetmode(set, MODE_BRCKTPASTE);
				break;
			/* Not implemented mouse modes. See comments there. */
			case 1001: /* mouse highlight mode; can hang the
				      terminal by design when implemented. */
			case 1005: /* UTF-8 mouse mode; will confuse
				      applications not supporting UTF-8
				      and luit. */
			case 1015: /* urxvt mangled mouse mode; incompatible
				      and can be mistaken for other control
				      codes. */
				break;
			default:
				std::cerr << "erresc: unknown private set/reset mode " << *args << "\n";
				break;
			}
		} else {
			switch (*args) {
			case 0:  /* Error (IGNORED) */
				break;
			case 2:
				xsetmode(set, MODE_KBDLOCK);
				break;
			case 4:  /* IRM -- Insertion-replacement */
				mode.set(Mode::INSERT, set);
				break;
			case 12: /* SRM -- Send/Receive */
				mode.set(Mode::TECHO, !set);
				break;
			case 20: /* LNM -- Linefeed/new line */
				mode.set(Mode::CRLF, set);
				break;
			default:
				std::cerr << "erresc: unknown set/reset mode " << *args << "\n";
				break;
			}
		}
	}
}

void Term::dumpLine(size_t n) const {
	char buf[utf8::UTF_SIZE];
	const Glyph *bp, *end;

	bp = &line[n][0];
	end = &bp[std::min(getLineLen(n), col) - 1];
	if (bp != end || bp->u != ' ') {
		for ( ; bp <= end; ++bp)
			m_tty->printToIoFile(buf, utf8::encode(bp->u, buf));
	}
	m_tty->printToIoFile("\n", 1);
}

bool Term::testAttrSet(const Glyph::Attr &attr) const {
	for (int i = 0; i < row-1; i++) {
		for (int j = 0; j < col-1; j++) {
			if (line[i][j].mode.test(attr))
				return 1;
		}
	}

	return 0;
}

void Term::setDirtyByAttr(const Glyph::Attr &attr) {
	for (int i = 0; i < row-1; i++) {
		for (int j = 0; j < col-1; j++) {
			if (line[i][j].mode.test(attr)) {
				setDirty(i, i);
				break;
			}
		}
	}
}

void Term::drawRegion(int x1, int y1, int x2, int y2) const {
	for (int y = y1; y < y2; y++) {
		if (!dirty[y])
			continue;

		dirty[y] = 0;
		xdrawline(line[y], x1, y, x2);
	}
}

void Term::draw() {
	if (!xstartdraw())
		return;

	int old_cx = c.x, old_ocx = ocx, old_ocy = ocy;

	/* adjust cursor position */
	ocx = std::clamp(ocx, 0, col-1);
	ocy = std::clamp(ocy, 0, row-1);

	if (line[ocy][ocx].mode.test(Attr::WDUMMY))
		ocx--;
	if (line[c.y][old_cx].mode.test(Attr::WDUMMY))
		old_cx--;

	drawRegion(0, 0, col, row);
	xdrawcursor(old_cx, c.y, line[c.y][old_cx], ocx, ocy, line[ocy][ocx]);
	ocx = old_cx;
	ocy = c.y;
	xfinishdraw();
	if (old_ocx != ocx || old_ocy != ocy)
		xximspot(ocx, ocy);
}

void Term::strSequence(unsigned char ch) {
	switch (ch) {
	case 0x90:   /* DCS -- Device Control String */
		ch = 'P';
		break;
	case 0x9f:   /* APC -- Application Program Command */
		ch = '_';
		break;
	case 0x9e:   /* PM -- Privacy Message */
		ch = '^';
		break;
	case 0x9d:   /* OSC -- Operating System Command */
		ch = ']';
		break;
	}

	strescseq.reset(ch);
	esc |= ESC_STR;
}

void Term::setChar(nst::Rune u, const nst::Glyph *attr, int x, int y) {
	constexpr const char *vt100_0[62] = { /* 0x41 - 0x7e */
		"↑", "↓", "→", "←", "█", "▚", "☃", /* A - G */
		0, 0, 0, 0, 0, 0, 0, 0, /* H - O */
		0, 0, 0, 0, 0, 0, 0, 0, /* P - W */
		0, 0, 0, 0, 0, 0, 0, " ", /* X - _ */
		"◆", "▒", "␉", "␌", "␍", "␊", "°", "±", /* ` - g */
		"␤", "␋", "┘", "┐", "┌", "└", "┼", "⎺", /* h - o */
		"⎻", "─", "⎼", "⎽", "├", "┤", "┴", "┬", /* p - w */
		"│", "≤", "≥", "π", "≠", "£", "·", /* x - ~ */
	};

	/*
	 * The table is proudly stolen from rxvt.
	 */
	if (trantbl[charset] == CS_GRAPHIC0 && in_range(u, 0x41, 0x7e) && vt100_0[u - 0x41])
		utf8::decode(vt100_0[u - 0x41], &u, utf8::UTF_SIZE);

	if (line[y][x].mode.test(Attr::WIDE)) {
		if (x+1 < col) {
			line[y][x+1].u = ' ';
			line[y][x+1].mode.reset(Attr::WDUMMY);
		}
	} else if (line[y][x].mode.test(Attr::WDUMMY)) {
		line[y][x-1].u = ' ';
		line[y][x-1].mode.reset(Attr::WIDE);
	}

	dirty[y] = 1;
	line[y][x] = *attr;
	line[y][x].u = u;
}

void Term::setDefTran(char ascii) {
	constexpr char cs[] = "0B";
	constexpr int vcs[] = {CS_GRAPHIC0, CS_USA};
	const char *p = strchr(cs, ascii);

	if (p == nullptr) {
		std::cerr << "esc unhandled charset: ESC ( " << ascii << "\n";
	} else {
		trantbl[icharset] = vcs[p - cs];
	}
}

void Term::decTest(char ch) {
	if (ch == '8') { /* DEC screen alignment test. */
		for (int x = 0; x < col; ++x) {
			for (int y = 0; y < row; ++y)
				setChar('E', &c.attr, x, y);
		}
	}
}

void Term::handleControlCode(uchar ascii) {
	switch (ascii) {
	case '\t':   /* HT */
		putTab(1);
		return;
	case '\b':   /* BS */
		moveTo(c.x - 1, c.y);
		return;
	case '\r':   /* CR */
		moveTo(0, c.y);
		return;
	case '\f':   /* LF */
	case '\v':   /* VT */
	case '\n':   /* LF */
		/* go to first col if the mode is set */
		putNewline(mode.test(Mode::CRLF));
		return;
	case '\a':   /* BEL */
		if (esc & ESC_STR_END) {
			/* backwards compatibility to xterm */
			strescseq.handle();
		} else {
			xbell();
		}
		break;
	case '\033': /* ESC */
		csiescseq = CSIEscape();
		esc &= ~(ESC_CSI|ESC_ALTCHARSET|ESC_TEST);
		esc |= ESC_START;
		return;
	case '\016': /* SO (LS1 -- Locking shift 1) */
	case '\017': /* SI (LS0 -- Locking shift 0) */
		charset = 1 - (ascii - '\016');
		return;
	case '\032': /* SUB */
		setChar('?', &c.attr, c.x, c.y);
		/* FALLTHROUGH */
	case '\030': /* CAN */
		csiescseq = CSIEscape();
		break;
	case '\005': /* ENQ (IGNORED) */
	case '\000': /* NUL (IGNORED) */
	case '\021': /* XON (IGNORED) */
	case '\023': /* XOFF (IGNORED) */
	case 0177:   /* DEL (IGNORED) */
		return;
	case 0x80:   /* TODO: PAD */
	case 0x81:   /* TODO: HOP */
	case 0x82:   /* TODO: BPH */
	case 0x83:   /* TODO: NBH */
	case 0x84:   /* TODO: IND */
		break;
	case 0x85:   /* NEL -- Next line */
		putNewline(); /* always go to first col */
		break;
	case 0x86:   /* TODO: SSA */
	case 0x87:   /* TODO: ESA */
		break;
	case 0x88:   /* HTS -- Horizontal tab stop */
		tabs[c.x] = 1;
		break;
	case 0x89:   /* TODO: HTJ */
	case 0x8a:   /* TODO: VTS */
	case 0x8b:   /* TODO: PLD */
	case 0x8c:   /* TODO: PLU */
	case 0x8d:   /* TODO: RI */
	case 0x8e:   /* TODO: SS2 */
	case 0x8f:   /* TODO: SS3 */
	case 0x91:   /* TODO: PU1 */
	case 0x92:   /* TODO: PU2 */
	case 0x93:   /* TODO: STS */
	case 0x94:   /* TODO: CCH */
	case 0x95:   /* TODO: MW */
	case 0x96:   /* TODO: SPA */
	case 0x97:   /* TODO: EPA */
	case 0x98:   /* TODO: SOS */
	case 0x99:   /* TODO: SGCI */
		break;
	case 0x9a:   /* DECID -- Identify Terminal */
		g_tty.write(nst::config::VTIDEN, strlen(nst::config::VTIDEN), 0);
		break;
	case 0x9b:   /* TODO: CSI */
	case 0x9c:   /* TODO: ST */
		break;
	case 0x90:   /* DCS -- Device Control String */
	case 0x9d:   /* OSC -- Operating System Command */
	case 0x9e:   /* PM -- Privacy Message */
	case 0x9f:   /* APC -- Application Program Command */
		strSequence(ascii);
		return;
	}
	/* only CAN, SUB, \a and C1 chars interrupt a sequence */
	esc &= ~(ESC_STR_END|ESC_STR);
}

void Term::putChar(Rune u) {
	char ch[nst::utf8::UTF_SIZE];
	int width, len;

	const int control = ISCONTROL(u);
	if (u < 127 || !mode.test(Mode::UTF8)) {
		ch[0] = u;
		width = len = 1;
	} else {
		len = nst::utf8::encode(u, ch);
		if (!control && (width = wcwidth(u)) == -1)
			width = 1;
	}

	if (mode.test(Mode::PRINT))
		g_tty.printToIoFile(ch, len);

	/*
	 * STR sequence must be checked before anything else
	 * because it uses all following characters until it
	 * receives a ESC, a SUB, a ST or any other C1 control
	 * character.
	 */
	if (esc & ESC_STR) {
		if (u == '\a' || u == 030 || u == 032 || u == 033 || ISCONTROLC1(u)) {
			esc &= ~(ESC_START|ESC_STR);
			esc |= ESC_STR_END;
			goto check_control_code;
		}

		strescseq.add(ch, len);
		return;
	}

check_control_code:
	/*
	 * Actions of control codes must be performed as soon they arrive
	 * because they can be embedded inside a control sequence, and
	 * they must not cause conflicts with sequences.
	 */
	if (control) {
		handleControlCode(u);
		/*
		 * control codes are not shown ever
		 */
		if (!esc)
			lastc = 0;
		return;
	} else if (esc & ESC_START) {
		if (esc & ESC_CSI) {
			csiescseq.buf[csiescseq.len++] = u;
			if (in_range(u, 0x40, 0x7E) || csiescseq.len >= sizeof(csiescseq.buf)-1) {
				esc = 0;
				csiescseq.parse();
				csiescseq.handle();
			}
			return;
		} else if (esc & ESC_UTF8) {
			switch (static_cast<char>(u)) {
			case 'G':
				mode.set(Mode::UTF8);
				break;
			case '@':
				mode.reset(Mode::UTF8);
				break;
			}
		} else if (esc & ESC_ALTCHARSET) {
			setDefTran(u);
		} else if (esc & ESC_TEST) {
			decTest(u);
		} else {
			if (!csiescseq.eschandle(u))
				return;
			/* sequence already finished */
		}
		esc = 0;
		/*
		 * All characters which form part of a sequence are not
		 * printed
		 */
		return;
	}
	if (g_sel.isSelected(c.x, c.y))
		g_sel.clear();

	nst::Glyph *gp = &line[c.y][c.x];
	if (mode.test(Mode::WRAP) && c.state.test(TCursor::State::WRAPNEXT)) {
		gp->mode.set(Attr::WRAP);
		putNewline();
		gp = &line[c.y][c.x];
	}

	if (mode.test(Mode::INSERT) && c.x + width < col)
		std::memmove(gp+width, gp, (col - c.x - width) * sizeof(nst::Glyph));

	if (c.x + width > col) {
		putNewline();
		gp = &line[c.y][c.x];
	}

	setChar(u, &c.attr, c.x, c.y);
	lastc = u;

	if (width == 2) {
		gp->mode.set(Attr::WIDE);
		if (c.x + 1 < col) {
			if (gp[1].mode.test(Attr::WIDE) && c.x+2 < col) {
				gp[2].u = ' ';
				gp[2].mode.reset(Attr::WDUMMY);
			}
			gp[1].u = '\0';
			gp[1].mode.limit(Attr::WDUMMY);
		}
	}
	if (c.x + width < col) {
		moveTo(c.x + width, c.y);
	} else {
		c.state.set(TCursor::State::WRAPNEXT);
	}
}

int Term::write(const char *buf, int buflen, int show_ctrl) {
	int charsize;
	nst::Rune u;
	int n;

	for (n = 0; n < buflen; n += charsize) {
		if (mode.test(Mode::UTF8)) {
			/* process a complete utf8 char */
			charsize = utf8::decode(buf + n, &u, buflen - n);
			if (charsize == 0)
				break;
		} else {
			u = buf[n] & 0xFF;
			charsize = 1;
		}
		if (show_ctrl && ISCONTROL(u)) {
			if (u & 0x80) {
				u &= 0x7f;
				putChar('^');
				putChar('[');
			} else if (u != '\n' && u != '\r' && u != '\t') {
				u ^= 0x40;
				putChar('^');
			}
		}
		putChar(u);
	}
	return n;
}

} // end ns
