// libc
#include <math.h>
#include <limits.h>
#include <locale.h>
#include <unistd.h>

// libX11 et al
#include <X11/Xatom.h>
#include <X11/Xlib.h>
#include <X11/cursorfont.h>
#include <X11/keysym.h>
#include <X11/Xft/Xft.h>
#include <X11/XKBlib.h>

// stdlib
#include <algorithm>
#include <iostream>
#include <map>
#include <string>

// cosmos
#include "cosmos/Init.hxx"
#include "cosmos/algs.hxx"
#include "cosmos/errors/ApiError.hxx"
#include "cosmos/errors/RuntimeError.hxx"
#include "cosmos/formatting.hxx"
#include "cosmos/io/Poller.hxx"
#include "cosmos/time/TimeSpec.hxx"
#include "cosmos/time/Clock.hxx"

// nst
#include "types.hxx"
#include "st.h"
#define FULL_NST_CONFIG
/* nst_config.h for applying patches and the configuration. */
#include "nst_config.h"
#include "Cmdline.hxx"
#include "codecs.hxx"
#include "Selection.hxx"
#include "Term.hxx"
#include "TTY.hxx"
#include "win.h"
#include "xtypes.hxx"

namespace nst {

typedef Glyph::Attr Attr;
typedef XftDraw *Draw;
typedef XftColor Color;
typedef XftGlyphFontSpec GlyphFontSpec;
using XEventCallback = void (*)(XEvent*);

/* Purely graphic info */
struct TermWindow {
	int tw, th; /* tty width and height */
	int w, h; /* window width and height */
	int ch; /* char height */
	int cw; /* char width  */
	WinModeMask mode; /* window state/mode flags */
	int cursor; /* cursor style */
};

struct XWindow {
	Display *dpy;
	Colormap cmap;
	Window win;
	Drawable buf;
	GlyphFontSpec *specbuf = nullptr; /* font spec buffer used for rendering */
	size_t specbuf_len = 0;
	Atom xembed, wmdeletewin, netwmname, netwmiconname, netwmpid;
	struct {
		XIM xim;
		XIC xic;
		XPoint spot;
		XVaNestedList spotlist;
	} ime;
	Draw draw;
	Visual *vis;
	XSetWindowAttributes attrs;
	int scr;
	int isfixed = False; /* is fixed geometry? */
	int l = 0, t = 0; /* left and top offset */
	int gm; /* geometry mask */
};

struct XSelection {
	Atom xtarget;
	std::string clipboard;
	std::string primary;
	cosmos::MonotonicStopWatch tclick1;
	cosmos::MonotonicStopWatch tclick2;
};

/* Font structure */
struct Font {
	int height;
	int width;
	int ascent;
	int descent;
	int badslant;
	int badweight;
	short lbearing;
	short rbearing;
	XftFont *match;
	FcFontSet *set;
	FcPattern *pattern;
};

/* Drawing Context */
struct DrawingContext {
	Color *col;
	size_t collen;
	Font font, bfont, ifont, ibfont;
	GC gc;
};

/* Font Ring Cache */
enum class FRC {
	NORMAL,
	ITALIC,
	BOLD,
	ITALICBOLD
};

struct Fontcache {
	XftFont *font;
	FRC flags;
	Rune unicodep;
};

static inline ushort sixd_to_16bit(size_t);
static int xmakeglyphfontspecs(XftGlyphFontSpec *, const Glyph *, int, int, int);
static void xdrawglyphfontspecs(const XftGlyphFontSpec *, Glyph, int, int, int);
static void xdrawglyph(Glyph, int, int);
static void xclear(int, int, int, int);
static int xgeommasktogravity(int);
static int ximopen(Display *);
static void ximinstantiate(Display *, XPointer, XPointer);
static void ximdestroy(XIM, XPointer, XPointer);
static int xicdestroy(XIC, XPointer, XPointer);
static void xinit(int, int);
static void cresize(int, int);
static void xresize(int, int);
static void xhints(void);
static int xloadcolor(size_t, const char *, Color *);
static int xloadfont(Font *, FcPattern *);
static void xloadfonts(const char *, double);
static void xunloadfont(Font *);
static void xunloadfonts(void);
static void xsetenv(void);
static void xseturgency(int);
static int evcol(XEvent *);
static int evrow(XEvent *);

static void expose(XEvent *);
static void visibility(XEvent *);
static void unmap(XEvent *);
static void kpress(XEvent *);
static void cmessage(XEvent *);
static void resize(XEvent *);
static void focus(XEvent *);
static void brelease(XEvent *);
static void bpress(XEvent *);
static void bmotion(XEvent *);
static void propnotify(XEvent *);
static void selnotify(XEvent *);
static void selclear_(XEvent *);
static void selrequest(XEvent *);
static void setsel(char *, Time);
static void mousereport(XEvent *);
static const char *kmap(KeySym, uint);
static bool match(uint, uint);

namespace {

const static std::map<int, XEventCallback> handlers = {
	{KeyPress, kpress},
	{ClientMessage, cmessage},
	{ConfigureNotify, resize},
	{VisibilityNotify, visibility},
	{UnmapNotify, unmap},
	{Expose, expose},
	{FocusIn, focus},
	{FocusOut, focus},
	{MotionNotify, bmotion},
	{ButtonPress, bpress},
	{ButtonRelease, brelease},
	{SelectionNotify, selnotify},
	/*
	 * PropertyNotify is only turned on when there is some INCR transfer happening
	 * for the selection retrieval.
	 */
	{PropertyNotify, propnotify},
	/*
	 * Uncomment if you want the selection to disappear when you select something
	 * different in another window.
	 */
#ifdef SELCLEAR
	{SelectionClear, selclear_},
#endif
	{SelectionRequest, selrequest}
};

const static std::map<int, unsigned> button_masks = {
	{Button1, Button1Mask},
	{Button2, Button2Mask},
	{Button3, Button3Mask},
	{Button4, Button4Mask},
	{Button5, Button5Mask},
};

/* Globals */
DrawingContext dc;
XWindow xw;
XSelection xsel;
TermWindow win;

/* Fontcache is an array now. A new font will be appended to the array. */
std::vector<Fontcache> frc;
const char *usedfont = nullptr;
double usedfontsize = 0;
double defaultfontsize = 0;

std::string opt_class;
std::string opt_name;
const char *opt_embed = nullptr;
const char *opt_font  = nullptr;
const char *opt_title = nullptr;
Cmdline cmdline;

uint buttons; /* bit field of pressed buttons */

unsigned int cols = config::COLS;
unsigned int rows = config::ROWS;

} // end anon ns

void clipcopy(const Arg *) {
	xsel.clipboard.clear();

	if (!xsel.primary.empty()) {
		xsel.clipboard = xsel.primary;
		Atom clipboard = XInternAtom(xw.dpy, "CLIPBOARD", 0);
		XSetSelectionOwner(xw.dpy, clipboard, xw.win, CurrentTime);
	}
}

void clippaste(const Arg *) {
	Atom clipboard = XInternAtom(xw.dpy, "CLIPBOARD", 0);
	XConvertSelection(xw.dpy, clipboard, xsel.xtarget, clipboard,
			xw.win, CurrentTime);
}

void selpaste(const Arg *) {
	XConvertSelection(xw.dpy, XA_PRIMARY, xsel.xtarget, XA_PRIMARY,
			xw.win, CurrentTime);
}

void numlock(const Arg *) {
	win.mode.flip(WinMode::NUMLOCK);
}

void zoom(const Arg *arg) {
	Arg larg;

	larg.f = (float)usedfontsize + arg->f;
	zoomabs(&larg);
}

void zoomabs(const Arg *arg) {
	xunloadfonts();
	xloadfonts(usedfont, arg->f);
	cresize(0, 0);
	term.redraw();
	xhints();
}

void zoomreset(const Arg *) {

	if (defaultfontsize > 0) {
		Arg larg;
		larg.f = defaultfontsize;
		zoomabs(&larg);
	}
}

void ttysend(const Arg *arg) {
	g_tty.write(arg->s, strlen(arg->s), 1);
}

void toggleprinter(const Arg *) {
	term.setPrintMode(!term.isPrintMode());
}

void printscreen(const Arg *) {
	term.dump();
}

void printsel(const Arg *) {
	g_sel.dump();
}

int evcol(XEvent *e) {
	int x = e->xbutton.x - config::BORDERPX;
	x = std::clamp(x, 0, win.tw - 1);
	return x / win.cw;
}

int evrow(XEvent *e) {
	int y = e->xbutton.y - config::BORDERPX;
	y = std::clamp(y, 0, win.th - 1);
	return y / win.ch;
}

static void mousesel(XEvent *e, bool done) {
	auto seltype = Selection::Type::REGULAR;
	uint state = e->xbutton.state & ~(Button1Mask | config::FORCEMOUSEMOD);

	for (unsigned type = 1; type < cosmos::num_elements(config::SELMASKS); ++type) {
		if (match(config::SELMASKS[type], state)) {
			seltype = static_cast<Selection::Type>(type);
			break;
		}
	}
	g_sel.extend(evcol(e), evrow(e), seltype, done);
	if (done) {
		setsel(g_sel.getSelection(), e->xbutton.time);
	}
}

void mousereport(XEvent *e) {
	int btn, code;
	int x = evcol(e), y = evrow(e);
	static int old_x, old_y;

	if (e->type == MotionNotify) {
		if (x == old_x && y == old_y)
			return;
		else if (!win.mode[WinMode::MOUSEMOTION] && !win.mode[WinMode::MOUSEMANY])
			return;
		/* MODE_MOUSEMOTION: no reporting if no button is pressed */
		else if (win.mode[WinMode::MOUSEMOTION] && buttons == 0)
			return;
		/* Set btn to lowest-numbered pressed button, or 12 if no
		 * buttons are pressed. */
		for (btn = 1; btn <= 11 && !(buttons & (1<<(btn-1))); btn++)
			;
		code = 32;
	} else {
		btn = e->xbutton.button;
		/* Only buttons 1 through 11 can be encoded */
		if (!cosmos::in_range(btn, 1, 11))
			return;
		if (e->type == ButtonRelease) {
			/* MODE_MOUSEX10: no button release reporting */
			if (win.mode[WinMode::MOUSEX10])
				return;
			/* Don't send release events for the scroll wheel */
			if (btn == 4 || btn == 5)
				return;
		}
		code = 0;
	}

	old_x = x;
	old_y = y;

	/* Encode btn into code. If no button is pressed for a motion event in
	 * MODE_MOUSEMANY, then encode it as a release. */
	if ((!win.mode[WinMode::MOUSESGR] && e->type == ButtonRelease) || btn == 12)
		code += 3;
	else if (btn >= 8)
		code += 128 + btn - 8;
	else if (btn >= 4)
		code += 64 + btn - 4;
	else
		code += btn - 1;

	if (!win.mode[WinMode::MOUSEX10]) {
		auto state = e->xbutton.state;
		code += ((state & ShiftMask  ) ?  4 : 0)
		      + ((state & Mod1Mask   ) ?  8 : 0) /* meta key: alt */
		      + ((state & ControlMask) ? 16 : 0);
	}

	int len;
	char buf[40];

	if (win.mode[WinMode::MOUSESGR]) {
		len = snprintf(buf, sizeof(buf), "\033[<%d;%d;%d%c",
				code, x+1, y+1,
				e->type == ButtonRelease ? 'm' : 'M');
	} else if (x < 223 && y < 223) {
		len = snprintf(buf, sizeof(buf), "\033[M%c%c%c",
				32+code, 32+x+1, 32+y+1);
	} else {
		return;
	}

	g_tty.write(buf, len, false);
}

uint buttonmask(uint button) {
	auto it = button_masks.find(button);
	return it == button_masks.end() ? 0 : it->second;
}

bool mouseaction(XEvent *e, uint release) {
	/* ignore Button<N>mask for Button<N> - it's set on release */
	uint state = e->xbutton.state & ~buttonmask(e->xbutton.button);

	for (
		const MouseShortcut *ms = config::MSHORTCUTS;
		ms < config::MSHORTCUTS + cosmos::num_elements(config::MSHORTCUTS);
		ms++) {

		if (ms->release == release &&
		    ms->button == e->xbutton.button &&
		    (match(ms->mod, state) ||  /* exact or forced */
		     match(ms->mod, state & ~config::FORCEMOUSEMOD))) {
			ms->func(&(ms->arg));
			return true;
		}
	}

	return false;
}

void bpress(XEvent *e) {
	const auto btn = e->xbutton.button;

	if (cosmos::in_range(btn, 1, 11))
		buttons |= 1 << (btn-1);

	if (win.mode[WinMode::MOUSE] && !(e->xbutton.state & config::FORCEMOUSEMOD)) {
		mousereport(e);
		return;
	}

	if (mouseaction(e, 0))
		return;

	if (btn == Button1) {
		/*
		 * If the user clicks below predefined timeouts specific
		 * snapping behaviour is exposed.
		 */
		Selection::Snap snap = Selection::Snap::NONE;

		if (xsel.tclick2.elapsed() <= config::TRIPLECLICKTIMEOUT) {
			snap = Selection::Snap::LINE;
		} else if (xsel.tclick1.elapsed() <= config::DOUBLECLICKTIMEOUT) {
			snap = Selection::Snap::WORD;
		}
		xsel.tclick2 = xsel.tclick1;
		xsel.tclick1.mark();

		g_sel.start(evcol(e), evrow(e), snap);
	}
}

void propnotify(XEvent *e) {
	Atom clipboard = XInternAtom(xw.dpy, "CLIPBOARD", 0);
	XPropertyEvent *xpev = &e->xproperty;

	if (xpev->state == PropertyNewValue &&
			(xpev->atom == XA_PRIMARY ||
			 xpev->atom == clipboard)) {
		selnotify(e);
	}
}

void selnotify(XEvent *e) {
	Atom property = None;

	if (e->type == SelectionNotify)
		property = e->xselection.property;
	else if (e->type == PropertyNotify)
		property = e->xproperty.atom;
	else
		return;

	ulong nitems, rem, ofs = 0;
	uchar *data, *last, *repl;
	Atom type;
	const Atom incratom = XInternAtom(xw.dpy, "INCR", 0);
	int format;

	do {
		if (XGetWindowProperty(xw.dpy, xw.win, property, ofs,
					BUFSIZ/4, False, AnyPropertyType,
					&type, &format, &nitems, &rem,
					&data)) {
			fprintf(stderr, "Clipboard allocation failed\n");
			return;
		}

		if (e->type == PropertyNotify && nitems == 0 && rem == 0) {
			/*
			 * If there is some PropertyNotify with no data, then
			 * this is the signal of the selection owner that all
			 * data has been transferred. We won't need to receive
			 * PropertyNotify events anymore.
			 */
			modifyBit(xw.attrs.event_mask, 0, PropertyChangeMask);
			XChangeWindowAttributes(xw.dpy, xw.win, CWEventMask,
					&xw.attrs);
		}

		if (type == incratom) {
			/*
			 * Activate the PropertyNotify events so we receive
			 * when the selection owner does send us the next
			 * chunk of data.
			 */
			modifyBit(xw.attrs.event_mask, 1, PropertyChangeMask);
			XChangeWindowAttributes(xw.dpy, xw.win, CWEventMask,
					&xw.attrs);

			/*
			 * Deleting the property is the transfer start signal.
			 */
			XDeleteProperty(xw.dpy, xw.win, (int)property);
			continue;
		}

		/*
		 * As seen in Selection::getSelection():
		 * Line endings are inconsistent in the terminal and GUI world
		 * copy and pasting. When receiving some selection data,
		 * replace all '\n' with '\r'.
		 * FIXME: Fix the computer world.
		 */
		repl = data;
		last = data + nitems * format / 8;
		while ((repl = (uchar*)memchr(repl, '\n', last - repl))) {
			*repl++ = '\r';
		}

		if (win.mode[WinMode::BRCKTPASTE] && ofs == 0)
			g_tty.write("\033[200~", 6, 0);
		g_tty.write((char *)data, nitems * format / 8, 1);
		if (win.mode[WinMode::BRCKTPASTE] && rem == 0)
			g_tty.write("\033[201~", 6, 0);
		XFree(data);
		/* number of 32-bit chunks returned */
		ofs += nitems * format / 32;
	} while (rem > 0);

	/*
	 * Deleting the property again tells the selection owner to send the
	 * next data chunk in the property.
	 */
	XDeleteProperty(xw.dpy, xw.win, (int)property);
}

void xclipcopy(void) {
	clipcopy(NULL);
}

[[maybe_unused]]
void selclear_(XEvent *) {
	g_sel.clear();
}

void selrequest(XEvent *e) {
	XSelectionRequestEvent *xsre;
	XSelectionEvent xev;
	Atom xa_targets, string;

	xsre = (XSelectionRequestEvent *) e;
	xev.type = SelectionNotify;
	xev.requestor = xsre->requestor;
	xev.selection = xsre->selection;
	xev.target = xsre->target;
	xev.time = xsre->time;
	if (xsre->property == None)
		xsre->property = xsre->target;

	/* reject */
	xev.property = None;

	xa_targets = XInternAtom(xw.dpy, "TARGETS", 0);
	if (xsre->target == xa_targets) {
		/* respond with the supported type */
		string = xsel.xtarget;
		XChangeProperty(xsre->display, xsre->requestor, xsre->property,
				XA_ATOM, 32, PropModeReplace,
				(uchar *) &string, 1);
		xev.property = xsre->property;
	} else if (xsre->target == xsel.xtarget || xsre->target == XA_STRING) {
		/*
		 * xith XA_STRING non ascii characters may be incorrect in the
		 * requestor. It is not our problem, use utf8.
		 */
		std::string *seltext = nullptr;
		Atom clipboard = XInternAtom(xw.dpy, "CLIPBOARD", 0);
		if (xsre->selection == XA_PRIMARY) {
			seltext = &xsel.primary;
		} else if (xsre->selection == clipboard) {
			seltext = &xsel.clipboard;
		} else {
			fprintf(stderr,
				"Unhandled clipboard selection 0x%lx\n",
				xsre->selection);
			return;
		}
		if (!seltext->empty()) {
			XChangeProperty(xsre->display, xsre->requestor,
					xsre->property, xsre->target,
					8, PropModeReplace,
					(uchar *)seltext->c_str(), seltext->size());
			xev.property = xsre->property;
		}
	}

	/* all done, send a notification to the listener */
	if (!XSendEvent(xsre->display, xsre->requestor, 1, 0, (XEvent *) &xev))
		fprintf(stderr, "Error sending SelectionNotify event\n");
}

void setsel(char *str, Time t) {
	if (!str)
		return;

	// this pointer originally comes from Selection::getSelection()
	// TODO: allocation/deallocation should be symmetric
	xsel.primary = str;
	delete[] str;

	XSetSelectionOwner(xw.dpy, XA_PRIMARY, xw.win, t);
	if (XGetSelectionOwner(xw.dpy, XA_PRIMARY) != xw.win)
		g_sel.clear();
}

void xsetsel(char *str) {
	setsel(str, CurrentTime);
}

void brelease(XEvent *e) {
	int btn = e->xbutton.button;

	if (1 <= btn && btn <= 11)
		buttons &= ~(1 << (btn-1));

	if (win.mode[WinMode::MOUSE] && !(e->xbutton.state & config::FORCEMOUSEMOD)) {
		mousereport(e);
		return;
	}

	if (mouseaction(e, 1))
		return;
	if (btn == Button1)
		mousesel(e, true);
}

void bmotion(XEvent *e) {
	if (win.mode[WinMode::MOUSE] && !(e->xbutton.state & config::FORCEMOUSEMOD)) {
		mousereport(e);
		return;
	}

	mousesel(e, false);
}

void cresize(int width, int height) {
	int col, row;

	if (width != 0)
		win.w = width;
	if (height != 0)
		win.h = height;

	col = (win.w - 2 * config::BORDERPX) / win.cw;
	row = (win.h - 2 * config::BORDERPX) / win.ch;
	col = std::max(1, col);
	row = std::max(1, row);

	term.resize(col, row);
	xresize(col, row);
	g_tty.resize(win.tw, win.th);
}

void xresize(int col, int row) {
	win.tw = col * win.cw;
	win.th = row * win.ch;

	XFreePixmap(xw.dpy, xw.buf);
	xw.buf = XCreatePixmap(xw.dpy, xw.win, win.w, win.h,
			DefaultDepth(xw.dpy, xw.scr));
	XftDrawChange(xw.draw, xw.buf);
	xclear(0, 0, win.w, win.h);

	/* resize to new width */
	xw.specbuf = renew(xw.specbuf, xw.specbuf_len, col);
	xw.specbuf_len = col;
}

ushort sixd_to_16bit(size_t x) {
	return x == 0 ? 0 : 0x3737 + 0x2828 * x;
}

int xloadcolor(size_t i, const char *name, Color *ncolor) {
	XRenderColor color = { 0, 0, 0, 0xfff };

	if (!name) {
		if (cosmos::in_range(i, 16, 255)) { /* 256 color */
			if (i < 6*6*6+16) { /* same colors as xterm */
				color.red   = sixd_to_16bit( ((i-16)/36)%6 );
				color.green = sixd_to_16bit( ((i-16)/6) %6 );
				color.blue  = sixd_to_16bit( ((i-16)/1) %6 );
			} else { /* greyscale */
				color.red = 0x0808 + 0x0a0a * (i - (6*6*6+16));
				color.green = color.blue = color.red;
			}
			return XftColorAllocValue(xw.dpy, xw.vis,
			                          xw.cmap, &color, ncolor);
		} else
			name = config::colorname[i];
	}

	return XftColorAllocName(xw.dpy, xw.vis, xw.cmap, name, ncolor);
}

void xloadcols(void) {
	size_t i;
	static int loaded;
	Color *cp;

	if (loaded) {
		for (cp = dc.col; cp < &dc.col[dc.collen]; ++cp)
			XftColorFree(xw.dpy, xw.vis, xw.cmap, cp);
	} else {
		dc.collen = std::max(cosmos::num_elements(config::colorname), 256UL);
		dc.col = new Color[dc.collen];
	}

	for (i = 0; i < dc.collen; i++)
		if (!xloadcolor(i, NULL, &dc.col[i])) {
			if (config::colorname[i])
				cosmos_throw (cosmos::ApiError(cosmos::sprintf("could not allocate color '%s'",
								config::colorname[i])));
			else
				cosmos_throw (cosmos::ApiError(cosmos::sprintf("could not allocate color %zd", i)));
		}
	loaded = 1;
}

int xgetcolor(size_t x, unsigned char *r, unsigned char *g, unsigned char *b) {
	if (x > dc.collen)
		return 1;

	*r = dc.col[x].color.red >> 8;
	*g = dc.col[x].color.green >> 8;
	*b = dc.col[x].color.blue >> 8;

	return 0;
}

int xsetcolorname(size_t x, const char *name) {
	Color ncolor;

	if (x > dc.collen)
		return 1;

	if (!xloadcolor(x, name, &ncolor))
		return 1;

	XftColorFree(xw.dpy, xw.vis, xw.cmap, &dc.col[x]);
	dc.col[x] = ncolor;

	return 0;
}

/*
 * Absolute coordinates.
 */
void xclear(int x1, int y1, int x2, int y2) {
	XftDrawRect(xw.draw,
			&dc.col[win.mode[WinMode::REVERSE]? config::DEFAULTFG : config::DEFAULTBG],
			x1, y1, x2-x1, y2-y1);
}

void xhints(void) {
	if (opt_name.empty()) {
		opt_name = config::TERMNAME;
	}
	if (opt_class.empty()) {
		opt_class = config::TERMNAME;
	}
	XClassHint clazz = {&opt_name[0], &opt_class[0]};
	XWMHints wm = {InputHint, 1, 0, 0, 0, 0, 0, 0, 0};
	XSizeHints *sizeh;

	sizeh = XAllocSizeHints();

	sizeh->flags = PSize | PResizeInc | PBaseSize | PMinSize;
	sizeh->height = win.h;
	sizeh->width = win.w;
	sizeh->height_inc = win.ch;
	sizeh->width_inc = win.cw;
	sizeh->base_height = 2 * config::BORDERPX;
	sizeh->base_width = 2 * config::BORDERPX;
	sizeh->min_height = win.ch + 2 * config::BORDERPX;
	sizeh->min_width = win.cw + 2 * config::BORDERPX;
	if (xw.isfixed) {
		sizeh->flags |= PMaxSize;
		sizeh->min_width = sizeh->max_width = win.w;
		sizeh->min_height = sizeh->max_height = win.h;
	}
	if (xw.gm & (XValue|YValue)) {
		sizeh->flags |= USPosition | PWinGravity;
		sizeh->x = xw.l;
		sizeh->y = xw.t;
		sizeh->win_gravity = xgeommasktogravity(xw.gm);
	}

	XSetWMProperties(xw.dpy, xw.win, NULL, NULL, NULL, 0, sizeh, &wm,
			&clazz);
	XFree(sizeh);
}

int xgeommasktogravity(int mask) {
	switch (mask & (XNegative|YNegative)) {
	case 0:
		return NorthWestGravity;
	case XNegative:
		return NorthEastGravity;
	case YNegative:
		return SouthWestGravity;
	}

	return SouthEastGravity;
}

int xloadfont(Font *f, FcPattern *pattern) {
	FcPattern *configured;
	FcPattern *match;
	FcResult result;
	XGlyphInfo extents;
	int wantattr, haveattr;

	/*
	 * Manually configure instead of calling XftMatchFont
	 * so that we can use the configured pattern for
	 * "missing glyph" lookups.
	 */
	configured = FcPatternDuplicate(pattern);
	if (!configured)
		return 1;

	FcConfigSubstitute(NULL, configured, FcMatchPattern);
	XftDefaultSubstitute(xw.dpy, xw.scr, configured);

	match = FcFontMatch(NULL, configured, &result);
	if (!match) {
		FcPatternDestroy(configured);
		return 1;
	}

	if (!(f->match = XftFontOpenPattern(xw.dpy, match))) {
		FcPatternDestroy(configured);
		FcPatternDestroy(match);
		return 1;
	}

	if ((XftPatternGetInteger(pattern, "slant", 0, &wantattr) ==
	    XftResultMatch)) {
		/*
		 * Check if xft was unable to find a font with the appropriate
		 * slant but gave us one anyway. Try to mitigate.
		 */
		if ((XftPatternGetInteger(f->match->pattern, "slant", 0,
		    &haveattr) != XftResultMatch) || haveattr < wantattr) {
			f->badslant = 1;
			fputs("font slant does not match\n", stderr);
		}
	}

	if ((XftPatternGetInteger(pattern, "weight", 0, &wantattr) ==
	    XftResultMatch)) {
		if ((XftPatternGetInteger(f->match->pattern, "weight", 0,
		    &haveattr) != XftResultMatch) || haveattr != wantattr) {
			f->badweight = 1;
			fputs("font weight does not match\n", stderr);
		}
	}

	XftTextExtentsUtf8(xw.dpy, f->match,
		(const FcChar8 *) config::ASCII_PRINTABLE,
		config::ASCII_PRINTABLE_LEN, &extents);

	f->set = NULL;
	f->pattern = configured;

	f->ascent = f->match->ascent;
	f->descent = f->match->descent;
	f->lbearing = 0;
	f->rbearing = f->match->max_advance_width;

	f->height = f->ascent + f->descent;
	f->width = (extents.xOff + config::ASCII_PRINTABLE_LEN - 1) / config::ASCII_PRINTABLE_LEN;

	return 0;
}

void xloadfonts(const char *fontstr, double fontsize) {
	FcPattern *pattern;
	double fontval;

	if (fontstr[0] == '-')
		pattern = XftXlfdParse(fontstr, False, False);
	else
		pattern = FcNameParse((const FcChar8 *)fontstr);

	if (!pattern)
		goto failed;

	if (fontsize > 1) {
		FcPatternDel(pattern, FC_PIXEL_SIZE);
		FcPatternDel(pattern, FC_SIZE);
		FcPatternAddDouble(pattern, FC_PIXEL_SIZE, (double)fontsize);
		usedfontsize = fontsize;
	} else {
		if (FcPatternGetDouble(pattern, FC_PIXEL_SIZE, 0, &fontval) ==
				FcResultMatch) {
			usedfontsize = fontval;
		} else if (FcPatternGetDouble(pattern, FC_SIZE, 0, &fontval) ==
				FcResultMatch) {
			usedfontsize = -1;
		} else {
			/*
			 * Default font size is 12, if none given. This is to
			 * have a known usedfontsize value.
			 */
			FcPatternAddDouble(pattern, FC_PIXEL_SIZE, 12);
			usedfontsize = 12;
		}
		defaultfontsize = usedfontsize;
	}

	if (xloadfont(&dc.font, pattern))
		goto failed;

	if (usedfontsize < 0) {
		FcPatternGetDouble(dc.font.match->pattern,
		                   FC_PIXEL_SIZE, 0, &fontval);
		usedfontsize = fontval;
		if (fontsize == 0)
			defaultfontsize = fontval;
	}

	/* Setting character width and height. */
	win.cw = ceilf(dc.font.width * config::CWSCALE);
	win.ch = ceilf(dc.font.height * config::CHSCALE);

	FcPatternDel(pattern, FC_SLANT);
	FcPatternAddInteger(pattern, FC_SLANT, FC_SLANT_ITALIC);
	if (xloadfont(&dc.ifont, pattern))
		goto failed;

	FcPatternDel(pattern, FC_WEIGHT);
	FcPatternAddInteger(pattern, FC_WEIGHT, FC_WEIGHT_BOLD);
	if (xloadfont(&dc.ibfont, pattern))
		goto failed;

	FcPatternDel(pattern, FC_SLANT);
	FcPatternAddInteger(pattern, FC_SLANT, FC_SLANT_ROMAN);
	if (xloadfont(&dc.bfont, pattern))
		goto failed;

	FcPatternDestroy(pattern);

	return;
failed:
	cosmos_throw (cosmos::RuntimeError(cosmos::sprintf("failed to open font %s", fontstr)));
}

void xunloadfont(Font *f) {
	XftFontClose(xw.dpy, f->match);
	FcPatternDestroy(f->pattern);
	if (f->set)
		FcFontSetDestroy(f->set);
}

void xunloadfonts(void) {
	/* Free the loaded fonts in the font cache.  */
	for (auto &fc: frc)
		XftFontClose(xw.dpy, fc.font);

	frc.clear();

	xunloadfont(&dc.font);
	xunloadfont(&dc.bfont);
	xunloadfont(&dc.ifont);
	xunloadfont(&dc.ibfont);
}

int ximopen(Display *) {
	XIMCallback imdestroy = { .client_data = NULL, .callback = ximdestroy };
	XICCallback icdestroy = { .client_data = NULL, .callback = xicdestroy };

	xw.ime.xim = XOpenIM(xw.dpy, NULL, NULL, NULL);
	if (xw.ime.xim == NULL)
		return 0;

	if (XSetIMValues(xw.ime.xim, XNDestroyCallback, &imdestroy, NULL))
		fprintf(stderr, "XSetIMValues: "
		                "Could not set XNDestroyCallback.\n");

	xw.ime.spotlist = XVaCreateNestedList(0, XNSpotLocation, &xw.ime.spot,
	                                      NULL);

	if (xw.ime.xic == NULL) {
		xw.ime.xic = XCreateIC(xw.ime.xim, XNInputStyle,
		                       XIMPreeditNothing | XIMStatusNothing,
		                       XNClientWindow, xw.win,
		                       XNDestroyCallback, &icdestroy,
		                       NULL);
	}
	if (xw.ime.xic == NULL)
		fprintf(stderr, "XCreateIC: Could not create input context.\n");

	return 1;
}

void ximinstantiate(Display *dpy, XPointer, XPointer) {
	if (ximopen(dpy))
		XUnregisterIMInstantiateCallback(xw.dpy, NULL, NULL, NULL,
		                                 ximinstantiate, NULL);
}

void ximdestroy(XIM, XPointer, XPointer) {
	xw.ime.xim = NULL;
	XRegisterIMInstantiateCallback(xw.dpy, NULL, NULL, NULL,
	                               ximinstantiate, NULL);
	XFree(xw.ime.spotlist);
}

int xicdestroy(XIC, XPointer, XPointer) {
	xw.ime.xic = NULL;
	return 1;
}

void xinit(int p_cols, int p_rows) {
	XGCValues gcvalues;
	Cursor cursor;
	Window parent;
	pid_t thispid = getpid();
	XColor xmousefg, xmousebg;

	if (!(xw.dpy = XOpenDisplay(NULL)))
		cosmos_throw (cosmos::RuntimeError("cannot open display"));
	xw.scr = XDefaultScreen(xw.dpy);
	xw.vis = XDefaultVisual(xw.dpy, xw.scr);

	/* font */
	if (!FcInit())
		cosmos_throw (cosmos::RuntimeError("could not init fontconfig"));

	usedfont = opt_font;
	xloadfonts(usedfont, 0);

	/* colors */
	xw.cmap = XDefaultColormap(xw.dpy, xw.scr);
	xloadcols();

	/* adjust fixed window geometry */
	win.w = 2 * config::BORDERPX + p_cols * win.cw;
	win.h = 2 * config::BORDERPX + p_rows * win.ch;
	if (xw.gm & XNegative)
		xw.l += DisplayWidth(xw.dpy, xw.scr) - win.w - 2;
	if (xw.gm & YNegative)
		xw.t += DisplayHeight(xw.dpy, xw.scr) - win.h - 2;

	/* Events */
	xw.attrs.background_pixel = dc.col[config::DEFAULTBG].pixel;
	xw.attrs.border_pixel = dc.col[config::DEFAULTBG].pixel;
	xw.attrs.bit_gravity = NorthWestGravity;
	xw.attrs.event_mask = FocusChangeMask | KeyPressMask | KeyReleaseMask
		| ExposureMask | VisibilityChangeMask | StructureNotifyMask
		| ButtonMotionMask | ButtonPressMask | ButtonReleaseMask;
	xw.attrs.colormap = xw.cmap;

	if (!(opt_embed && (parent = strtol(opt_embed, NULL, 0))))
		parent = XRootWindow(xw.dpy, xw.scr);
	xw.win = XCreateWindow(xw.dpy, parent, xw.l, xw.t,
			win.w, win.h, 0, XDefaultDepth(xw.dpy, xw.scr), InputOutput,
			xw.vis, CWBackPixel | CWBorderPixel | CWBitGravity
			| CWEventMask | CWColormap, &xw.attrs);

	memset(&gcvalues, 0, sizeof(gcvalues));
	gcvalues.graphics_exposures = False;
	dc.gc = XCreateGC(xw.dpy, parent, GCGraphicsExposures,
			&gcvalues);
	xw.buf = XCreatePixmap(xw.dpy, xw.win, win.w, win.h,
			DefaultDepth(xw.dpy, xw.scr));
	XSetForeground(xw.dpy, dc.gc, dc.col[config::DEFAULTBG].pixel);
	XFillRectangle(xw.dpy, xw.buf, dc.gc, 0, 0, win.w, win.h);

	/* font spec buffer */
	xw.specbuf = new GlyphFontSpec[p_cols];
	xw.specbuf_len = p_cols;

	/* Xft rendering context */
	xw.draw = XftDrawCreate(xw.dpy, xw.buf, xw.vis, xw.cmap);

	/* input methods */
	if (!ximopen(xw.dpy)) {
		XRegisterIMInstantiateCallback(xw.dpy, NULL, NULL, NULL,
	                                       ximinstantiate, NULL);
	}

	/* white cursor, black outline */
	cursor = XCreateFontCursor(xw.dpy, config::MOUSESHAPE);
	XDefineCursor(xw.dpy, xw.win, cursor);

	if (XParseColor(xw.dpy, xw.cmap, config::colorname[config::MOUSEFG], &xmousefg) == 0) {
		xmousefg.red   = 0xffff;
		xmousefg.green = 0xffff;
		xmousefg.blue  = 0xffff;
	}

	if (XParseColor(xw.dpy, xw.cmap, config::colorname[config::MOUSEBG], &xmousebg) == 0) {
		xmousebg.red   = 0x0000;
		xmousebg.green = 0x0000;
		xmousebg.blue  = 0x0000;
	}

	XRecolorCursor(xw.dpy, cursor, &xmousefg, &xmousebg);

	xw.xembed = XInternAtom(xw.dpy, "_XEMBED", False);
	xw.wmdeletewin = XInternAtom(xw.dpy, "WM_DELETE_WINDOW", False);
	xw.netwmname = XInternAtom(xw.dpy, "_NET_WM_NAME", False);
	xw.netwmiconname = XInternAtom(xw.dpy, "_NET_WM_ICON_NAME", False);
	XSetWMProtocols(xw.dpy, xw.win, &xw.wmdeletewin, 1);

	xw.netwmpid = XInternAtom(xw.dpy, "_NET_WM_PID", False);
	XChangeProperty(xw.dpy, xw.win, xw.netwmpid, XA_CARDINAL, 32,
			PropModeReplace, (uchar *)&thispid, 1);

	win.mode = WinModeMask(WinMode::NUMLOCK);
	xsettitle(NULL);
	xhints();
	XMapWindow(xw.dpy, xw.win);
	XSync(xw.dpy, False);

	xsel.tclick1.mark();
	xsel.tclick2.mark();
	xsel.primary.clear();
	xsel.clipboard.clear();
	xsel.xtarget = XInternAtom(xw.dpy, "UTF8_STRING", 0);
	if (xsel.xtarget == None)
		xsel.xtarget = XA_STRING;
}

int xmakeglyphfontspecs(
		XftGlyphFontSpec *specs, const Glyph *glyphs,
		int len, int x, int y) {
	float winx = config::BORDERPX + x * win.cw, winy = config::BORDERPX + y * win.ch;
	Font *fnt = &dc.font;
	FRC frcflags = FRC::NORMAL;
	float runewidth = win.cw;
	Rune rune;
	FT_UInt glyphidx;
	FcResult fcres;
	FcPattern *fcpattern, *fontpattern;
	FcFontSet *fcsets[] = { NULL };
	FcCharSet *fccharset;
	int numspecs = 0;
	Glyph::AttrBitMask prevmode(Glyph::AttrBitMask::all);

	for (int i = 0, xp = winx, yp = winy + fnt->ascent; i < len; ++i) {
		/* Fetch rune and mode for current glyph. */
		rune = glyphs[i].u;
		const auto &mode = glyphs[i].mode;

		/* Skip dummy wide-character spacing. */
		if (mode == Glyph::AttrBitMask({Attr::WDUMMY}))
			continue;

		/* Determine font for glyph if different from previous glyph. */
		if (prevmode != mode) {
			prevmode = mode;
			fnt = &dc.font;
			frcflags = FRC::NORMAL;
			runewidth = win.cw * (mode.test(Attr::WIDE) ? 2.0f : 1.0f);
			if (mode.test(Attr::ITALIC) && mode.test(Attr::BOLD)) {
				fnt = &dc.ibfont;
				frcflags = FRC::ITALICBOLD;
			} else if (mode.test(Attr::ITALIC)) {
				fnt = &dc.ifont;
				frcflags = FRC::ITALIC;
			} else if (mode.test(Attr::BOLD)) {
				fnt = &dc.bfont;
				frcflags = FRC::BOLD;
			}
			yp = winy + fnt->ascent;
		}

		/* Lookup character index with default font. */
		glyphidx = XftCharIndex(xw.dpy, fnt->match, rune);
		if (glyphidx) {
			specs[numspecs].font = fnt->match;
			specs[numspecs].glyph = glyphidx;
			specs[numspecs].x = (short)xp;
			specs[numspecs].y = (short)yp;
			xp += runewidth;
			numspecs++;
			continue;
		}

		Fontcache *font_entry = nullptr;
		/* Fallback on font cache, search the font cache for match. */
		for (auto &fc: frc) {
			glyphidx = XftCharIndex(xw.dpy, fc.font, rune);
			/* Everything correct. */
			if (glyphidx && fc.flags == frcflags) {
				font_entry = &fc;
				break;
			}
			/* We got a default font for a not found glyph. */
			else if (!glyphidx && fc.flags == frcflags
					&& fc.unicodep == rune) {
				font_entry = &fc;
				break;
			}
		}

		/* Nothing was found. Use fontconfig to find matching font. */
		if (!font_entry) {
			if (!fnt->set)
				fnt->set = FcFontSort(0, fnt->pattern,
				                       1, 0, &fcres);
			fcsets[0] = fnt->set;

			/*
			 * Nothing was found in the cache. Now use
			 * some dozen of Fontconfig calls to get the
			 * font for one single character.
			 *
			 * Xft and fontconfig are design failures.
			 */
			fcpattern = FcPatternDuplicate(fnt->pattern);
			fccharset = FcCharSetCreate();

			FcCharSetAddChar(fccharset, rune);
			FcPatternAddCharSet(fcpattern, FC_CHARSET,
					fccharset);
			FcPatternAddBool(fcpattern, FC_SCALABLE, 1);

			FcConfigSubstitute(0, fcpattern,
					FcMatchPattern);
			FcDefaultSubstitute(fcpattern);

			fontpattern = FcFontSetMatch(0, fcsets, 1,
					fcpattern, &fcres);

			/* Allocate memory for the new cache entry. */

			auto font = XftFontOpenPattern(xw.dpy, fontpattern);
			if (!font)
				cosmos_throw (cosmos::ApiError("XftFontOpenPattern failed seeking fallback font"));
			frc.emplace_back(Fontcache{font, frcflags, rune});

			glyphidx = XftCharIndex(xw.dpy, frc.back().font, rune);

			font_entry = &frc.back();

			FcPatternDestroy(fcpattern);
			FcCharSetDestroy(fccharset);
		}

		specs[numspecs].font = font_entry->font;
		specs[numspecs].glyph = glyphidx;
		specs[numspecs].x = (short)xp;
		specs[numspecs].y = (short)yp;
		xp += runewidth;
		numspecs++;
	}

	return numspecs;
}

static void setRenderColor(XRenderColor &out, const uint32_t in) {
	/* seems like the X color values are 16-bit wide and we need to
	 * translate the one color bytes into the upper byte in the
	 * XRenderColor */
	out.alpha = 0xffff;
	out.red = (in & 0xff0000) >> 8;
	out.green = (in & 0xff00);
	out.blue = (in & 0xff) << 8;
}

void xdrawglyphfontspecs(const XftGlyphFontSpec *specs, Glyph base, int len, int x, int y) {
	int charlen = len * (base.mode.test(Attr::WIDE) ? 2 : 1);
	int winx = config::BORDERPX + x * win.cw, winy = config::BORDERPX + y * win.ch,
	    width = charlen * win.cw;
	Color *fg, *bg, *temp, revfg, revbg, truefg, truebg;
	XRenderColor colfg, colbg;
	XRectangle r;

	/* Fallback on color display for attributes not supported by the font */
	if (base.mode.test(Attr::ITALIC) && base.mode.test(Attr::BOLD)) {
		if (dc.ibfont.badslant || dc.ibfont.badweight)
			base.fg = config::DEFAULTATTR;
	} else if ((base.mode.test(Attr::ITALIC) && dc.ifont.badslant) ||
	    (base.mode.test(Attr::BOLD) && dc.bfont.badweight)) {
		base.fg = config::DEFAULTATTR;
	}

	if (base.isFgTrueColor()) {
		setRenderColor(colfg, base.fg);
		XftColorAllocValue(xw.dpy, xw.vis, xw.cmap, &colfg, &truefg);
		fg = &truefg;
	} else {
		fg = &dc.col[base.fg];
	}

	if (base.isBgTrueColor()) {
		setRenderColor(colbg, base.bg);
		XftColorAllocValue(xw.dpy, xw.vis, xw.cmap, &colbg, &truebg);
		bg = &truebg;
	} else {
		bg = &dc.col[base.bg];
	}

	/* Change basic system colors [0-7] to bright system colors [8-15] */
	if (base.mode[Attr::BOLD] && !base.mode[Attr::FAINT] && base.fg <= 7)
		fg = &dc.col[base.fg + 8];

	if (win.mode[WinMode::REVERSE]) {
		if (fg == &dc.col[config::DEFAULTFG]) {
			fg = &dc.col[config::DEFAULTBG];
		} else {
			colfg.red = ~fg->color.red;
			colfg.green = ~fg->color.green;
			colfg.blue = ~fg->color.blue;
			colfg.alpha = fg->color.alpha;
			XftColorAllocValue(xw.dpy, xw.vis, xw.cmap, &colfg,
					&revfg);
			fg = &revfg;
		}

		if (bg == &dc.col[config::DEFAULTBG]) {
			bg = &dc.col[config::DEFAULTFG];
		} else {
			colbg.red = ~bg->color.red;
			colbg.green = ~bg->color.green;
			colbg.blue = ~bg->color.blue;
			colbg.alpha = bg->color.alpha;
			XftColorAllocValue(xw.dpy, xw.vis, xw.cmap, &colbg,
					&revbg);
			bg = &revbg;
		}
	}

	if (base.mode.test(Attr::FAINT) && !base.mode[Attr::BOLD]) {
		colfg.red = fg->color.red / 2;
		colfg.green = fg->color.green / 2;
		colfg.blue = fg->color.blue / 2;
		colfg.alpha = fg->color.alpha;
		XftColorAllocValue(xw.dpy, xw.vis, xw.cmap, &colfg, &revfg);
		fg = &revfg;
	}

	if (base.mode.test(Attr::REVERSE)) {
		temp = fg;
		fg = bg;
		bg = temp;
	}

	if (base.mode.test(Attr::BLINK) && win.mode[WinMode::BLINK])
		fg = bg;

	if (base.mode.test(Attr::INVISIBLE))
		fg = bg;

	/* Intelligent cleaning up of the borders. */
	if (x == 0) {
		xclear(0, (y == 0)? 0 : winy, config::BORDERPX,
			winy + win.ch +
			((winy + win.ch >= config::BORDERPX + win.th)? win.h : 0));
	}
	if (winx + width >= config::BORDERPX + win.tw) {
		xclear(winx + width, (y == 0)? 0 : winy, win.w,
			((winy + win.ch >= config::BORDERPX + win.th)? win.h : (winy + win.ch)));
	}
	if (y == 0)
		xclear(winx, 0, winx + width, config::BORDERPX);
	if (winy + win.ch >= config::BORDERPX + win.th)
		xclear(winx, winy + win.ch, winx + width, win.h);

	/* Clean up the region we want to draw to. */
	XftDrawRect(xw.draw, bg, winx, winy, width, win.ch);

	/* Set the clip region because Xft is sometimes dirty. */
	r.x = 0;
	r.y = 0;
	r.height = win.ch;
	r.width = width;
	XftDrawSetClipRectangles(xw.draw, winx, winy, &r, 1);

	/* Render the glyphs. */
	XftDrawGlyphFontSpec(xw.draw, fg, specs, len);

	/* Render underline and strikethrough. */
	if (base.mode.test(Attr::UNDERLINE)) {
		XftDrawRect(xw.draw, fg, winx, winy + dc.font.ascent + 1,
				width, 1);
	}

	if (base.mode.test(Attr::STRUCK)) {
		XftDrawRect(xw.draw, fg, winx, winy + 2 * dc.font.ascent / 3,
				width, 1);
	}

	/* Reset clip to none. */
	XftDrawSetClip(xw.draw, 0);
}

void xdrawglyph(Glyph g, int x, int y) {
	int numspecs;
	XftGlyphFontSpec spec;

	numspecs = xmakeglyphfontspecs(&spec, &g, 1, x, y);
	xdrawglyphfontspecs(&spec, g, numspecs, x, y);
}

void xdrawcursor(int cx, int cy, Glyph g, int ox, int oy, Glyph og) {
	Color drawcol;

	/* remove the old cursor */
	if (g_sel.isSelected(ox, oy))
		og.mode.flip(Attr::REVERSE);
	xdrawglyph(og, ox, oy);

	if (win.mode[WinMode::HIDE])
		return;

	/*
	 * Select the right color for the right mode.
	 */
	g.mode.limit({Attr::BOLD, Attr::ITALIC, Attr::UNDERLINE, Attr::STRUCK, Attr::WIDE});

	if (win.mode[WinMode::REVERSE]) {
		g.mode.set(Attr::REVERSE);
		g.bg = config::DEFAULTFG;
		if (g_sel.isSelected(cx, cy)) {
			drawcol = dc.col[config::DEFAULTCS];
			g.fg = config::DEFAULTRCS;
		} else {
			drawcol = dc.col[config::DEFAULTRCS];
			g.fg = config::DEFAULTCS;
		}
	} else {
		if (g_sel.isSelected(cx, cy)) {
			g.fg = config::DEFAULTFG;
			g.bg = config::DEFAULTRCS;
		} else {
			g.fg = config::DEFAULTBG;
			g.bg = config::DEFAULTCS;
		}
		drawcol = dc.col[g.bg];
	}

	/* draw the new one */
	if (win.mode[WinMode::FOCUSED]) {
		switch (win.cursor) {
		case 7: /* st extension */
			g.u = 0x2603; /* snowman (U+2603) */
			/* FALLTHROUGH */
		case 0: /* Blinking Block */
		case 1: /* Blinking Block (Default) */
		case 2: /* Steady Block */
			xdrawglyph(g, cx, cy);
			break;
		case 3: /* Blinking Underline */
		case 4: /* Steady Underline */
			XftDrawRect(xw.draw, &drawcol,
					config::BORDERPX + cx * win.cw,
					config::BORDERPX + (cy + 1) * win.ch - \
						config::CURSORTHICKNESS,
					win.cw, config::CURSORTHICKNESS);
			break;
		case 5: /* Blinking bar */
		case 6: /* Steady bar */
			XftDrawRect(xw.draw, &drawcol,
					config::BORDERPX + cx * win.cw,
					config::BORDERPX + cy * win.ch,
					config::CURSORTHICKNESS, win.ch);
			break;
		}
	} else {
		XftDrawRect(xw.draw, &drawcol,
				config::BORDERPX + cx * win.cw,
				config::BORDERPX + cy * win.ch,
				win.cw - 1, 1);
		XftDrawRect(xw.draw, &drawcol,
				config::BORDERPX + cx * win.cw,
				config::BORDERPX + cy * win.ch,
				1, win.ch - 1);
		XftDrawRect(xw.draw, &drawcol,
				config::BORDERPX + (cx + 1) * win.cw - 1,
				config::BORDERPX + cy * win.ch,
				1, win.ch - 1);
		XftDrawRect(xw.draw, &drawcol,
				config::BORDERPX + cx * win.cw,
				config::BORDERPX + (cy + 1) * win.ch - 1,
				win.cw, 1);
	}
}

void xsetenv(void) {
	setenv("WINDOWID", std::to_string(xw.win).c_str(), 1);
}

void xseticontitle(const char *p) {
	XTextProperty prop;
	setDefault(p, opt_title);

	if (Xutf8TextListToTextProperty(xw.dpy, (char**)&p, 1, XUTF8StringStyle,
	                                &prop) != Success)
		return;
	XSetWMIconName(xw.dpy, xw.win, &prop);
	XSetTextProperty(xw.dpy, xw.win, &prop, xw.netwmiconname);
	XFree(prop.value);
}

void xsettitle(const char *p) {
	XTextProperty prop;
	setDefault(p, opt_title);

	if (Xutf8TextListToTextProperty(xw.dpy, (char**)&p, 1, XUTF8StringStyle,
	                                &prop) != Success)
		return;
	XSetWMName(xw.dpy, xw.win, &prop);
	XSetTextProperty(xw.dpy, xw.win, &prop, xw.netwmname);
	XFree(prop.value);
}

int xstartdraw(void) {
	return win.mode[WinMode::VISIBLE];
}

bool attrsDiffer(const Glyph &a, const Glyph &b) {
	return a.mode != b.mode || a.fg != b.fg || a.bg != b.bg;
}

void xdrawline(const Line &line, int x1, int y1, int x2) {
	int i, x, ox, numspecs;
	Glyph base, newone;
	XftGlyphFontSpec *specs = xw.specbuf;

	numspecs = xmakeglyphfontspecs(specs, &line[x1], x2 - x1, x1, y1);
	i = ox = 0;
	for (x = x1; x < x2 && i < numspecs; x++) {
		newone = line[x];
		if (newone.mode.only(Attr::WDUMMY))
			continue;
		if (g_sel.isSelected(x, y1))
			newone.mode.flip(Attr::REVERSE);
		if (i > 0 && attrsDiffer(base, newone)) {
			xdrawglyphfontspecs(specs, base, i, ox, y1);
			specs += i;
			numspecs -= i;
			i = 0;
		}
		if (i == 0) {
			ox = x;
			base = newone;
		}
		i++;
	}
	if (i > 0)
		xdrawglyphfontspecs(specs, base, i, ox, y1);
}

void xfinishdraw(void) {
	XCopyArea(xw.dpy, xw.buf, xw.win, dc.gc, 0, 0, win.w,
			win.h, 0, 0);
	XSetForeground(xw.dpy, dc.gc,
			dc.col[win.mode[WinMode::REVERSE]?
				config::DEFAULTFG : config::DEFAULTBG].pixel);
}

void xximspot(int x, int y) {
	if (xw.ime.xic == NULL)
		return;

	xw.ime.spot.x = config::BORDERPX + x * win.cw;
	xw.ime.spot.y = config::BORDERPX + (y + 1) * win.ch;

	XSetICValues(xw.ime.xic, XNPreeditAttributes, xw.ime.spotlist, NULL);
}

void expose(XEvent *) {
	term.redraw();
}

void visibility(XEvent *ev) {
	XVisibilityEvent *e = &ev->xvisibility;

	win.mode.set(WinMode::VISIBLE, e->state != VisibilityFullyObscured);
}

void unmap(XEvent *) {
	win.mode.reset(WinMode::VISIBLE);
}

void xsetpointermotion(int set) {
	modifyBit(xw.attrs.event_mask, set, PointerMotionMask);
	XChangeWindowAttributes(xw.dpy, xw.win, CWEventMask, &xw.attrs);
}

void xsetmode(bool set, const WinMode &flag) {
	auto mode = win.mode;
	win.mode.set(flag, set);
	if (win.mode[WinMode::REVERSE] != mode[WinMode::REVERSE])
		term.redraw();
}

int xsetcursor(int cursor) {
	if (!cosmos::in_range(cursor, 0, 7)) /* 7: st extension */
		return 1;
	win.cursor = cursor;
	return 0;
}

void xseturgency(int add) {
	XWMHints *h = XGetWMHints(xw.dpy, xw.win);

	modifyBit(h->flags, add, XUrgencyHint);
	XSetWMHints(xw.dpy, xw.win, h);
	XFree(h);
}

void xbell(void) {
	if (!(win.mode[WinMode::FOCUSED]))
		xseturgency(1);
	if (config::BELLVOLUME)
		XkbBell(xw.dpy, xw.win, config::BELLVOLUME, (Atom)NULL);
}

void focus(XEvent *ev) {
	XFocusChangeEvent *e = &ev->xfocus;

	if (e->mode == NotifyGrab)
		return;

	if (ev->type == FocusIn) {
		if (xw.ime.xic)
			XSetICFocus(xw.ime.xic);
		win.mode.set(WinMode::FOCUSED);
		xseturgency(0);
		if (win.mode[WinMode::FOCUS])
			g_tty.write("\033[I", 3, 0);
	} else {
		if (xw.ime.xic)
			XUnsetICFocus(xw.ime.xic);
		win.mode.reset(WinMode::FOCUSED);
		if (win.mode[WinMode::FOCUS])
			g_tty.write("\033[O", 3, 0);
	}
}

bool match(uint mask, uint state) {
	return mask == XK_ANY_MOD || mask == (state & ~config::IGNOREMOD);
}

const char* kmap(KeySym k, uint state) {
	const Key *kp;
	size_t i;

	/* Check for mapped keys out of X11 function keys. */
	for (i = 0; i < cosmos::num_elements(config::MAPPEDKEYS); i++) {
		if (config::MAPPEDKEYS[i] == k)
			break;
	}
	if (i == cosmos::num_elements(config::MAPPEDKEYS)) {
		if ((k & 0xFFFF) < 0xFD00)
			return NULL;
	}

	for (kp = config::KEY; kp < config::KEY + cosmos::num_elements(config::KEY); kp++) {
		if (kp->k != k)
			continue;

		if (!match(kp->mask, state))
			continue;

		if (win.mode[WinMode::APPKEYPAD] ? kp->appkey < 0 : kp->appkey > 0)
			continue;
		if (win.mode[WinMode::NUMLOCK] && kp->appkey == 2)
			continue;

		if (win.mode[WinMode::APPCURSOR] ? kp->appcursor < 0 : kp->appcursor > 0)
			continue;

		return kp->s;
	}

	return NULL;
}

void kpress(XEvent *ev) {
	XKeyEvent *e = &ev->xkey;
	KeySym ksym;
	char buf[64];
	const char *customkey = nullptr;
	int len;
	Rune c;
	Status status;
	const Shortcut *bp;

	if (win.mode[WinMode::KBDLOCK])
		return;

	if (xw.ime.xic)
		len = XmbLookupString(xw.ime.xic, e, buf, sizeof buf, &ksym, &status);
	else
		len = XLookupString(e, buf, sizeof buf, &ksym, NULL);
	/* 1. shortcuts */
	for (bp = config::SHORTCUTS; bp < config::SHORTCUTS + cosmos::num_elements(config::SHORTCUTS); bp++) {
		if (ksym == bp->keysym && match(bp->mod, e->state)) {
			bp->func(&(bp->arg));
			return;
		}
	}

	/* 2. custom keys from nst_config.h */
	if ((customkey = kmap(ksym, e->state))) {
		g_tty.write(customkey, strlen(customkey), 1);
		return;
	}

	/* 3. composed string from input method */
	if (len == 0)
		return;
	if (len == 1 && e->state & Mod1Mask) {
		if (win.mode[WinMode::EIGHT_BIT]) {
			if (*buf < 0177) {
				c = *buf | 0x80;
				len = utf8::encode(c, buf);
			}
		} else {
			buf[1] = buf[0];
			buf[0] = '\033';
			len = 2;
		}
	}
	g_tty.write(buf, len, 1);
}

void cmessage(XEvent *e) {
	/*
	 * See xembed specs
	 *  http://standards.freedesktop.org/xembed-spec/xembed-spec-latest.html
	 */
	if (e->xclient.message_type == xw.xembed && e->xclient.format == 32) {
		if (e->xclient.data.l[1] == XEMBED_FOCUS_IN) {
			win.mode.set(WinMode::FOCUSED);
			xseturgency(0);
		} else if (e->xclient.data.l[1] == XEMBED_FOCUS_OUT) {
			win.mode.reset(WinMode::FOCUSED);
		}
	} else if ((Atom)e->xclient.data.l[0] == xw.wmdeletewin) {
		g_tty.hangup();
		exit(0);
	}
}

void resize(XEvent *e) {
	if (e->xconfigure.width == win.w && e->xconfigure.height == win.h)
		return;

	cresize(e->xconfigure.width, e->xconfigure.height);
}

void waitForWindowMapping() {
	XEvent ev;
	int w = win.w, h = win.h;

	/* Waiting for window mapping */
	do {
		XNextEvent(xw.dpy, &ev);
		/*
		 * This XFilterEvent call is required because of XOpenIM. It
		 * does filter out the key event and some client message for
		 * the input method too.
		 */
		if (XFilterEvent(&ev, None))
			continue;
		if (ev.type == ConfigureNotify) {
			w = ev.xconfigure.width;
			h = ev.xconfigure.height;
		}
	} while (ev.type != MapNotify);

	cresize(w, h);
}

static void run() {
	auto ttyfd = g_tty.create(cmdline);

	waitForWindowMapping();

	auto childfd = g_tty.getChildFD();
	auto xfd = cosmos::FileDescriptor(XConnectionNumber(xw.dpy));
	XEvent ev;
	bool drawing = false;
	cosmos::MonotonicStopWatch draw_watch, blink_watch(cosmos::MonotonicStopWatch::InitialMark(true));
	cosmos::TimeSpec trigger;
	std::chrono::milliseconds timeout(-1);
	cosmos::Poller poller;

	poller.create();
	for (auto fd: {ttyfd, xfd, childfd}) {
		poller.addFD(fd, cosmos::Poller::MonitorMask({cosmos::Poller::MonitorSetting::INPUT}));
	}

	while (true) {
		if (XPending(xw.dpy))
			timeout = std::chrono::milliseconds(0);  /* existing events might not set xfd */

		auto events = poller.wait(timeout.count() >= 0 ?
				std::optional<std::chrono::milliseconds>(timeout) :
				std::nullopt);

		bool tty_ev = false;
		bool x_ev = false;

		for (const auto &event: events) {
			if (event.fd() == childfd)
				g_tty.sigChildEvent();
			else if (event.fd() == ttyfd) {
				g_tty.read();
				tty_ev = true;
			}
		}

		while (XPending(xw.dpy)) {
			x_ev = true;
			XNextEvent(xw.dpy, &ev);
			if (XFilterEvent(&ev, None))
				continue;
			else if (auto it = handlers.find(ev.type); it != handlers.end()) {
				auto handler = it->second;
				handler(&ev);
			}
		}

		/*
		 * To reduce flicker and tearing, when new content or event
		 * triggers drawing, we first wait a bit to ensure we got
		 * everything, and if nothing new arrives - we draw.
		 * We start with trying to wait minlatency ms. If more content
		 * arrives sooner, we retry with shorter and shorter periods,
		 * and eventually draw even without idle after MAXLATENCY ms.
		 * Typically this results in low latency while interacting,
		 * maximum latency intervals during `cat huge.txt`, and perfect
		 * sync with periodic updates from animations/key-repeats/etc.
		 */
		if (tty_ev || x_ev) {
			if (!drawing) {
				draw_watch.mark();
				drawing = true;
			}

			const auto diff = draw_watch.elapsed();
			timeout = (config::MAXLATENCY - diff) / config::MAXLATENCY * config::MINLATENCY;

			if (timeout.count() > 0)
				continue;  /* we have time, try to find idle */
		}

		/* idle detected or maxlatency exhausted -> draw */
		timeout = std::chrono::milliseconds(-1);
		if (config::BLINKTIMEOUT.count() > 0 && term.testAttrSet(Attr::BLINK)) {
			timeout = config::BLINKTIMEOUT - blink_watch.elapsed();
			if (timeout.count() <= 0) {
				if (-timeout.count() > config::BLINKTIMEOUT.count()) /* start visible */
					win.mode.set(WinMode::BLINK);
				win.mode.flip(WinMode::BLINK);
				term.setDirtyByAttr(Attr::BLINK);
				blink_watch.mark();
				timeout = config::BLINKTIMEOUT;
			}
		}

		term.draw();
		XFlush(xw.dpy);
		drawing = false;
	}
}

void fixup_colornames() {
	for (size_t index = 0; index < cosmos::num_elements(config::EXTENDED_COLORS); index++) {
		config::colorname[256+index] = config::EXTENDED_COLORS[index];
	}
}

void applyCmdline(const Cmdline &cmd) {
	if (cmd.use_alt_screen.isSet()) {
		term.setAllowAltScreen(cmd.use_alt_screen.getValue());
	} else {
		term.setAllowAltScreen(config::ALLOWALTSCREEN);
	}

	if (cmd.window_class.isSet()) {
		opt_class = cmd.window_class.getValue();
	}

	if (cmd.window_name.isSet()) {
		opt_name = cmd.window_name.getValue();
	}

	if (cmd.fixed_geometry.isSet()) {
		xw.isfixed = True;
	}

	if (cmd.window_geometry.isSet()) {
		xw.gm = XParseGeometry(
			cmd.window_geometry.getValue().c_str(),
			&xw.l, &xw.t, &cols, &rows
		);
	}

	if (cmd.embed_window.isSet()) {
		opt_embed = cmd.embed_window.getValue().c_str();
	}

	opt_title = cmd.window_title.getValue().c_str();
	opt_font = cmd.font.getValue().c_str();

	auto &rest = cmd.rest.getValue();

	if (!cmd.window_title.isSet() && !cmd.tty_line.isSet() && !rest.empty()) {
		// use command basename as title
		opt_title = rest[0].c_str();
	}
}

void main(int argc, const char **argv) {
	cosmos::Init init;
	fixup_colornames();
	xsetcursor(config::CURSORSHAPE);

	cmdline.parse(argc, argv);
	cols = std::max(cols, 1U);
	rows = std::max(rows, 1U);
	term = Term(cols, rows);
	applyCmdline(cmdline);

	setlocale(LC_CTYPE, "");
	XSetLocaleModifiers("");
	xinit(cols, rows);
	xsetenv();
	run();
}

} // end ns nst

int main(int argc, const char **argv) {
	try {
		nst::main(argc, argv);
	} catch (const std::exception &ex) {
		std::cerr << ex.what() << std::endl;
		return EXIT_FAILURE;
	}

	return 0;
}
