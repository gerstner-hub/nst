#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "st.h"
#include "win.h"
#include "macros.hxx"
#include "nst_config.h"
#include "Selection.hxx"
#include "Term.hxx"
#include "TTY.hxx"
#include "StringEscape.hxx"
#include "CSIEscape.hxx"

// cosmos
#include "cosmos/algs.hxx"

using cosmos::in_range;
typedef nst::Glyph::Attr Attr;
using namespace nst;

static void tsetchar(nst::Rune, const nst::Glyph *, int, int);
static void tcontrolcode(uchar );
static void tdectest(char );
static void tdefutf8(char);
static void tdeftran(char);

void
die(const char *errstr, ...)
{
	va_list ap;

	va_start(ap, errstr);
	vfprintf(stderr, errstr, ap);
	va_end(ap);
	exit(1);
}

void
tsetchar(nst::Rune u, const nst::Glyph *attr, int x, int y)
{
	static const char *vt100_0[62] = { /* 0x41 - 0x7e */
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
	if (term.trantbl[term.charset] == CS_GRAPHIC0 &&
	   in_range(u, 0x41, 0x7e) && vt100_0[u - 0x41])
		nst::utf8::decode(vt100_0[u - 0x41], &u, nst::utf8::UTF_SIZE);

	if (term.line[y][x].mode.test(Attr::WIDE)) {
		if (x+1 < term.col) {
			term.line[y][x+1].u = ' ';
			term.line[y][x+1].mode.reset(Attr::WDUMMY);
		}
	} else if (term.line[y][x].mode.test(Attr::WDUMMY)) {
		term.line[y][x-1].u = ' ';
		term.line[y][x-1].mode.reset(Attr::WIDE);
	}

	term.dirty[y] = 1;
	term.line[y][x] = *attr;
	term.line[y][x].u = u;
}

void
toggleprinter(const Arg *)
{
	term.mode.flip(Term::Mode::PRINT);
}

void
printscreen(const Arg *)
{
	term.dump();
}

void
printsel(const Arg *)
{
	g_sel.dump();
}

void
tdefutf8(char ascii)
{
	if (ascii == 'G')
		term.mode.set(Term::Mode::UTF8);
	else if (ascii == '@')
		term.mode.reset(Term::Mode::UTF8);
}

void
tdeftran(char ascii)
{
	static char cs[] = "0B";
	static int vcs[] = {CS_GRAPHIC0, CS_USA};
	char *p;

	if ((p = strchr(cs, ascii)) == NULL) {
		fprintf(stderr, "esc unhandled charset: ESC ( %c\n", ascii);
	} else {
		term.trantbl[term.icharset] = vcs[p - cs];
	}
}

void
tdectest(char c)
{
	int x, y;

	if (c == '8') { /* DEC screen alignment test. */
		for (x = 0; x < term.col; ++x) {
			for (y = 0; y < term.row; ++y)
				tsetchar('E', &term.c.attr, x, y);
		}
	}
}

void
tcontrolcode(uchar ascii)
{
	switch (ascii) {
	case '\t':   /* HT */
		term.putTab(1);
		return;
	case '\b':   /* BS */
		term.moveTo(term.c.x-1, term.c.y);
		return;
	case '\r':   /* CR */
		term.moveTo(0, term.c.y);
		return;
	case '\f':   /* LF */
	case '\v':   /* VT */
	case '\n':   /* LF */
		/* go to first col if the mode is set */
		term.putNewline(term.mode.test(Term::Mode::CRLF));
		return;
	case '\a':   /* BEL */
		if (term.esc & ESC_STR_END) {
			/* backwards compatibility to xterm */
			strescseq.handle();
		} else {
			xbell();
		}
		break;
	case '\033': /* ESC */
		csiescseq = CSIEscape();
		term.esc &= ~(ESC_CSI|ESC_ALTCHARSET|ESC_TEST);
		term.esc |= ESC_START;
		return;
	case '\016': /* SO (LS1 -- Locking shift 1) */
	case '\017': /* SI (LS0 -- Locking shift 0) */
		term.charset = 1 - (ascii - '\016');
		return;
	case '\032': /* SUB */
		tsetchar('?', &term.c.attr, term.c.x, term.c.y);
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
		term.putNewline(); /* always go to first col */
		break;
	case 0x86:   /* TODO: SSA */
	case 0x87:   /* TODO: ESA */
		break;
	case 0x88:   /* HTS -- Horizontal tab stop */
		term.tabs[term.c.x] = 1;
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
		term.strSequence(ascii);
		return;
	}
	/* only CAN, SUB, \a and C1 chars interrupt a sequence */
	term.esc &= ~(ESC_STR_END|ESC_STR);
}

void
tputc(nst::Rune u)
{
	char c[nst::utf8::UTF_SIZE];
	int control;
	int width, len;
	nst::Glyph *gp;

	control = ISCONTROL(u);
	if (u < 127 || !term.mode.test(Term::Mode::UTF8)) {
		c[0] = u;
		width = len = 1;
	} else {
		len = nst::utf8::encode(u, c);
		if (!control && (width = wcwidth(u)) == -1)
			width = 1;
	}

	if (term.mode.test(Term::Mode::PRINT))
		g_tty.printToIoFile(c, len);

	/*
	 * STR sequence must be checked before anything else
	 * because it uses all following characters until it
	 * receives a ESC, a SUB, a ST or any other C1 control
	 * character.
	 */
	if (term.esc & ESC_STR) {
		if (u == '\a' || u == 030 || u == 032 || u == 033 ||
		   ISCONTROLC1(u)) {
			term.esc &= ~(ESC_START|ESC_STR);
			term.esc |= ESC_STR_END;
			goto check_control_code;
		}

		if (strescseq.len+len >= strescseq.siz) {
			/*
			 * Here is a bug in terminals. If the user never sends
			 * some code to stop the str or esc command, then st
			 * will stop responding. But this is better than
			 * silently failing with unknown characters. At least
			 * then users will report back.
			 *
			 * In the case users ever get fixed, here is the code:
			 */
			/*
			 * term.esc = 0;
			 * strescseq.handle();
			 */
			if (strescseq.siz > (SIZE_MAX - nst::utf8::UTF_SIZE) / 2)
				return;
			strescseq.resize(strescseq.siz << 1);
		}

		memmove(&strescseq.buf[strescseq.len], c, len);
		strescseq.len += len;
		return;
	}

check_control_code:
	/*
	 * Actions of control codes must be performed as soon they arrive
	 * because they can be embedded inside a control sequence, and
	 * they must not cause conflicts with sequences.
	 */
	if (control) {
		tcontrolcode(u);
		/*
		 * control codes are not shown ever
		 */
		if (!term.esc)
			term.lastc = 0;
		return;
	} else if (term.esc & ESC_START) {
		if (term.esc & ESC_CSI) {
			csiescseq.buf[csiescseq.len++] = u;
			if (in_range(u, 0x40, 0x7E)
					|| csiescseq.len >= \
					sizeof(csiescseq.buf)-1) {
				term.esc = 0;
				csiescseq.parse();
				csiescseq.handle();
			}
			return;
		} else if (term.esc & ESC_UTF8) {
			tdefutf8(u);
		} else if (term.esc & ESC_ALTCHARSET) {
			tdeftran(u);
		} else if (term.esc & ESC_TEST) {
			tdectest(u);
		} else {
			if (!csiescseq.eschandle(u))
				return;
			/* sequence already finished */
		}
		term.esc = 0;
		/*
		 * All characters which form part of a sequence are not
		 * printed
		 */
		return;
	}
	if (g_sel.isSelected(term.c.x, term.c.y))
		g_sel.clear();

	gp = &term.line[term.c.y][term.c.x];
	if (term.mode.test(Term::Mode::WRAP) && (term.c.state.test(Term::TCursor::State::WRAPNEXT))) {
		gp->mode.set(Attr::WRAP);
		term.putNewline();
		gp = &term.line[term.c.y][term.c.x];
	}

	if (term.mode.test(Term::Mode::INSERT) && term.c.x+width < term.col)
		memmove(gp+width, gp, (term.col - term.c.x - width) * sizeof(nst::Glyph));

	if (term.c.x+width > term.col) {
		term.putNewline();
		gp = &term.line[term.c.y][term.c.x];
	}

	tsetchar(u, &term.c.attr, term.c.x, term.c.y);
	term.lastc = u;

	if (width == 2) {
		gp->mode.set(Attr::WIDE);
		if (term.c.x+1 < term.col) {
			if (gp[1].mode.test(Attr::WIDE) && term.c.x+2 < term.col) {
				gp[2].u = ' ';
				gp[2].mode.reset(Attr::WDUMMY);
			}
			gp[1].u = '\0';
			gp[1].mode.limit(Attr::WDUMMY);
		}
	}
	if (term.c.x+width < term.col) {
		term.moveTo(term.c.x+width, term.c.y);
	} else {
		term.c.state.set(Term::TCursor::State::WRAPNEXT);
	}
}

int
twrite(const char *buf, int buflen, int show_ctrl)
{
	int charsize;
	nst::Rune u;
	int n;

	for (n = 0; n < buflen; n += charsize) {
		if (term.mode.test(Term::Mode::UTF8)) {
			/* process a complete utf8 char */
			charsize = nst::utf8::decode(buf + n, &u, buflen - n);
			if (charsize == 0)
				break;
		} else {
			u = buf[n] & 0xFF;
			charsize = 1;
		}
		if (show_ctrl && ISCONTROL(u)) {
			if (u & 0x80) {
				u &= 0x7f;
				tputc('^');
				tputc('[');
			} else if (u != '\n' && u != '\r' && u != '\t') {
				u ^= 0x40;
				tputc('^');
			}
		}
		tputc(u);
	}
	return n;
}
