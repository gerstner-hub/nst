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
#include "cosmos/types.hxx"
#include "cosmos/errors/ApiError.hxx"
#include "cosmos/errors/RuntimeError.hxx"
#include "cosmos/formatting.hxx"
#include "cosmos/io/Poller.hxx"
#include "cosmos/proc/Process.hxx"
#include "cosmos/time/TimeSpec.hxx"
#include "cosmos/time/Clock.hxx"

// X++
#include "X++/XDisplay.hxx"
#include "X++/XAtom.hxx"

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
typedef XftColor Color;
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
	xpp::XDisplay *display = nullptr;
	xpp::XAtomMapper *mapper = nullptr;
	Colormap cmap;
	Window win;
	Drawable buf;
	std::vector<XftGlyphFontSpec> specbuf; /* font spec buffer used for rendering */
	Atom xembed, wmdeletewin, netwmname, netwmiconname, netwmpid;
	struct {
		XIM xim;
		XIC xic;
		XPoint spot;
		XVaNestedList spotlist;
	} ime;
	XftDraw *draw;
	Visual *vis;
	XSetWindowAttributes attrs;
	int scr;
	bool isfixed = false; /* is fixed geometry? */
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
	std::vector<Color> col;
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

static size_t xmakeglyphfontspecs(XftGlyphFontSpec *, const Glyph *, size_t, int, int);
static void xdrawglyphfontspecs(const XftGlyphFontSpec *, Glyph, size_t, int, int);
static void xdrawglyph(Glyph, int, int);
static void xclear(int, int, int, int);
static int xgeommasktogravity(int);
static int ximopen();
static void ximinstantiate(Display *, XPointer, XPointer);
static void ximdestroy(XIM, XPointer, XPointer);
static int xicdestroy(XIC, XPointer, XPointer);
static void xinit();
static void cresize(int, int);
static void xresize(int, int);
static void xhints(void);
static int xloadcolor(size_t, const char *, Color *);
static int xloadfont(Font *, FcPattern *);
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
static void xloadfontsOrThrow(const char*, double fontsize);

namespace {

const std::map<int, XEventCallback> handlers = {
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

const std::map<int, unsigned> button_masks = {
	{Button1, Button1Mask},
	{Button2, Button2Mask},
	{Button3, Button3Mask},
	{Button4, Button4Mask},
	{Button5, Button5Mask},
};

struct FcPatternGuard : public cosmos::ResourceGuard<FcPattern*> {
	explicit FcPatternGuard(FcPattern *p) :
		ResourceGuard(p, [](FcPattern *_p) { FcPatternDestroy(_p); })
	{}
};
struct FcCharSetGuard : public cosmos::ResourceGuard<FcCharSet*> {
	explicit FcCharSetGuard(FcCharSet *p) :
		ResourceGuard(p, [](FcCharSet *_p) { FcCharSetDestroy(_p); })
	{}
};

/* Globals */
DrawingContext dc;
XWindow xw;
XSelection xsel;
TermWindow twin;

/* Fontcache is an array now. A new font will be appended to the array. */
std::vector<Fontcache> frc;
const char *usedfont = nullptr;
double usedfontsize = 0;
double defaultfontsize = 0;

Cmdline cmdline;

PressedButtons buttons; /* bit field of pressed buttons */

unsigned int cols = config::COLS;
unsigned int rows = config::ROWS;

inline Display* getDisplay() {
	return static_cast<Display*>(*xw.display);
}

inline Atom getAtom(const char *name) {
	return (xw.mapper)->getAtom(name);
}

} // end anon ns

void clipcopy(const Arg *) {
	xsel.clipboard.clear();

	if (!xsel.primary.empty()) {
		xsel.clipboard = xsel.primary;
		Atom clipboard = getAtom("CLIPBOARD");
		XSetSelectionOwner(getDisplay(), clipboard, xw.win, CurrentTime);
	}
}

void clippaste(const Arg *) {
	Atom clipboard = getAtom("CLIPBOARD");
	XConvertSelection(getDisplay(), clipboard, xsel.xtarget, clipboard,
			xw.win, CurrentTime);
}

void selpaste(const Arg *) {
	XConvertSelection(getDisplay(), XA_PRIMARY, xsel.xtarget, XA_PRIMARY,
			xw.win, CurrentTime);
}

void numlock(const Arg *) {
	twin.mode.flip(WinMode::NUMLOCK);
}

void zoom(const Arg *arg) {
	Arg larg;

	larg.f = (float)usedfontsize + arg->f;
	zoomabs(&larg);
}

void zoomabs(const Arg *arg) {
	xunloadfonts();
	xloadfontsOrThrow(usedfont, arg->f);
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
	x = std::clamp(x, 0, twin.tw - 1);
	return x / twin.cw;
}

int evrow(XEvent *e) {
	int y = e->xbutton.y - config::BORDERPX;
	y = std::clamp(y, 0, twin.th - 1);
	return y / twin.ch;
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
		else if (!twin.mode[WinMode::MOUSEMOTION] && !twin.mode[WinMode::MOUSEMANY])
			return;
		/* MODE_MOUSEMOTION: no reporting if no button is pressed */
		else if (twin.mode[WinMode::MOUSEMOTION] && buttons.none())
			return;
		/* Set btn to lowest-numbered pressed button, or NO_BUTTON if no
		 * buttons are pressed. */
		btn = buttons.getFirstButton();
		code = 32;
	} else {
		btn = e->xbutton.button;
		/* Only buttons 1 through 11 can be encoded */
		if (!buttons.valid(btn))
			return;
		if (e->type == ButtonRelease) {
			/* MODE_MOUSEX10: no button release reporting */
			if (twin.mode[WinMode::MOUSEX10])
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
	if ((!twin.mode[WinMode::MOUSESGR] && e->type == ButtonRelease) || btn == PressedButtons::NO_BUTTON)
		code += 3;
	else if (btn >= 8)
		code += 128 + btn - 8;
	else if (btn >= 4)
		code += 64 + btn - 4;
	else
		code += btn - 1;

	if (!twin.mode[WinMode::MOUSEX10]) {
		auto state = e->xbutton.state;
		code += ((state & ShiftMask  ) ?  4 : 0)
		      + ((state & Mod1Mask   ) ?  8 : 0) /* meta key: alt */
		      + ((state & ControlMask) ? 16 : 0);
	}

	int len;
	char buf[40];

	if (twin.mode[WinMode::MOUSESGR]) {
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

bool mouseaction(XEvent *e, bool release) {
	/* ignore Button<N>mask for Button<N> - it's set on release */
	uint state = e->xbutton.state & ~buttonmask(e->xbutton.button);

	for (auto &ms: config::MSHORTCUTS) {
		if (ms.release == release &&
		    ms.button == e->xbutton.button &&
		    (match(ms.mod, state) ||  /* exact or forced */
		     match(ms.mod, state & ~config::FORCEMOUSEMOD))) {
			ms.func(&(ms.arg));
			return true;
		}
	}

	return false;
}

void bpress(XEvent *e) {
	const auto btn = e->xbutton.button;

	if (buttons.valid(btn))
		buttons.setPressed(btn);

	if (twin.mode[WinMode::MOUSE] && !(e->xbutton.state & config::FORCEMOUSEMOD)) {
		mousereport(e);
		return;
	}

	if (mouseaction(e, false))
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
	Atom clipboard = getAtom("CLIPBOARD");
	XPropertyEvent *xpev = &e->xproperty;

	if (xpev->state == PropertyNewValue &&
			(xpev->atom == XA_PRIMARY ||
			 xpev->atom == clipboard)) {
		selnotify(e);
	}
}

void selnotify(XEvent *e) {
	Atom property = None;

	switch (e->type) {
	case SelectionNotify:
		property = e->xselection.property;
		break;
	case PropertyNotify:
		property = e->xproperty.atom;
		break;
	default:
		return;
	}

	ulong nitems, rem, ofs = 0;
	uchar *data, *last, *repl;
	Atom type;
	const Atom incratom = getAtom("INCR");
	int format;

	do {
		if (XGetWindowProperty(getDisplay(), xw.win, property, ofs,
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
			XChangeWindowAttributes(getDisplay(), xw.win, CWEventMask,
					&xw.attrs);
		}

		if (type == incratom) {
			/*
			 * Activate the PropertyNotify events so we receive
			 * when the selection owner does send us the next
			 * chunk of data.
			 */
			modifyBit(xw.attrs.event_mask, 1, PropertyChangeMask);
			XChangeWindowAttributes(getDisplay(), xw.win, CWEventMask,
					&xw.attrs);

			/*
			 * Deleting the property is the transfer start signal.
			 */
			XDeleteProperty(getDisplay(), xw.win, (int)property);
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

		if (twin.mode[WinMode::BRCKTPASTE] && ofs == 0)
			g_tty.write("\033[200~", 6, 0);
		g_tty.write((char *)data, nitems * format / 8, 1);
		if (twin.mode[WinMode::BRCKTPASTE] && rem == 0)
			g_tty.write("\033[201~", 6, 0);
		XFree(data);
		/* number of 32-bit chunks returned */
		ofs += nitems * format / 32;
	} while (rem > 0);

	/*
	 * Deleting the property again tells the selection owner to send the
	 * next data chunk in the property.
	 */
	XDeleteProperty(getDisplay(), xw.win, (int)property);
}

void xclipcopy(void) {
	clipcopy(nullptr);
}

[[maybe_unused]]
void selclear_(XEvent *) {
	g_sel.clear();
}

void selrequest(XEvent *e) {

	XSelectionRequestEvent *xsre = (XSelectionRequestEvent *) e;
	XSelectionEvent xev;
	xev.type = SelectionNotify;
	xev.requestor = xsre->requestor;
	xev.selection = xsre->selection;
	xev.target = xsre->target;
	xev.time = xsre->time;

	if (xsre->property == None)
		xsre->property = xsre->target;

	/* reject */
	xev.property = None;
	const Atom xa_targets = getAtom("TARGETS");

	if (xsre->target == xa_targets) {
		/* respond with the supported type */
		Atom string = xsel.xtarget;
		XChangeProperty(xsre->display, xsre->requestor, xsre->property,
				XA_ATOM, 32, PropModeReplace,
				(uchar *) &string, 1);
		xev.property = xsre->property;
	} else if (xsre->target == xsel.xtarget || xsre->target == XA_STRING) {
		/*
		 * with XA_STRING non ascii characters may be incorrect in the
		 * requestor. It is not our problem, use utf8.
		 */
		std::string *seltext = nullptr;
		const Atom clipboard = getAtom("CLIPBOARD");
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

	XSetSelectionOwner(getDisplay(), XA_PRIMARY, xw.win, t);
	if (XGetSelectionOwner(getDisplay(), XA_PRIMARY) != xw.win)
		g_sel.clear();
}

void xsetsel(char *str) {
	setsel(str, CurrentTime);
}

void brelease(XEvent *e) {
	int btn = e->xbutton.button;

	if (buttons.valid(btn))
		buttons.setReleased(btn);

	if (twin.mode[WinMode::MOUSE] && !(e->xbutton.state & config::FORCEMOUSEMOD)) {
		mousereport(e);
		return;
	}

	if (mouseaction(e, true))
		return;
	if (btn == Button1)
		mousesel(e, true);
}

void bmotion(XEvent *e) {
	if (twin.mode[WinMode::MOUSE] && !(e->xbutton.state & config::FORCEMOUSEMOD)) {
		mousereport(e);
		return;
	}

	mousesel(e, false);
}

void cresize(int width, int height) {

	if (width != 0)
		twin.w = width;
	if (height != 0)
		twin.h = height;

	int col = (twin.w - 2 * config::BORDERPX) / twin.cw;
	int row = (twin.h - 2 * config::BORDERPX) / twin.ch;
	col = std::max(1, col);
	row = std::max(1, row);

	term.resize(col, row);
	xresize(col, row);
	g_tty.resize(twin.tw, twin.th);
}

void xresize(int col, int row) {
	twin.tw = col * twin.cw;
	twin.th = row * twin.ch;

	XFreePixmap(getDisplay(), xw.buf);
	xw.buf = XCreatePixmap(getDisplay(), xw.win, twin.w, twin.h,
			DefaultDepth(getDisplay(), xw.scr));
	XftDrawChange(xw.draw, xw.buf);
	xclear(0, 0, twin.w, twin.h);

	/* resize to new width */
	xw.specbuf.resize(col);
}

static inline uint16_t sixd_to_16bit(size_t x) {
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
			return XftColorAllocValue(getDisplay(), xw.vis,
			                          xw.cmap, &color, ncolor);
		} else
			name = config::colorname[i];
	}

	return XftColorAllocName(getDisplay(), xw.vis, xw.cmap, name, ncolor);
}

void xloadcols(void) {
	static bool loaded;

	if (loaded) {
		for (auto &c: dc.col) {
			XftColorFree(getDisplay(), xw.vis, xw.cmap, &c);
		}
	} else {
		auto len = std::max(cosmos::num_elements(config::colorname), 256UL);
		dc.col.resize(len);
	}

	for (size_t i = 0; i < dc.col.size(); i++) {
		if (!xloadcolor(i, nullptr, &dc.col[i])) {
			if (config::colorname[i])
				cosmos_throw (cosmos::ApiError(cosmos::sprintf("could not allocate color '%s'",
								config::colorname[i])));
			else
				cosmos_throw (cosmos::ApiError(cosmos::sprintf("could not allocate color %zd", i)));
		}
	}

	loaded = true;
}

int xgetcolor(size_t x, unsigned char *r, unsigned char *g, unsigned char *b) {
	if (x >= dc.col.size())
		return 1;

	*r = dc.col[x].color.red >> 8;
	*g = dc.col[x].color.green >> 8;
	*b = dc.col[x].color.blue >> 8;

	return 0;
}

int xsetcolorname(size_t x, const char *name) {
	if (x >= dc.col.size())
		return 1;

	Color ncolor;
	if (!xloadcolor(x, name, &ncolor))
		return 1;

	XftColorFree(getDisplay(), xw.vis, xw.cmap, &dc.col[x]);
	dc.col[x] = ncolor;

	return 0;
}

/*
 * Absolute coordinates.
 */
void xclear(int x1, int y1, int x2, int y2) {
	const auto colindex = twin.mode[WinMode::REVERSE] ? config::DEFAULTFG : config::DEFAULTBG;
	XftDrawRect(xw.draw, &dc.col[colindex], x1, y1, x2-x1, y2-y1);
}

void xhints(void) {
	auto &wname = cmdline.window_name.getValue();
	auto &wclass = cmdline.window_class.getValue();
	XClassHint clazz = {&wname[0], &wclass[0]};
	XWMHints wm = {InputHint, 1, 0, 0, 0, 0, 0, 0, 0};
	XSizeHints *sizeh = XAllocSizeHints();

	sizeh->flags = PSize | PResizeInc | PBaseSize | PMinSize;
	sizeh->height = twin.h;
	sizeh->width = twin.w;
	sizeh->height_inc = twin.ch;
	sizeh->width_inc = twin.cw;
	sizeh->base_height = 2 * config::BORDERPX;
	sizeh->base_width = 2 * config::BORDERPX;
	sizeh->min_height = twin.ch + 2 * config::BORDERPX;
	sizeh->min_width = twin.cw + 2 * config::BORDERPX;
	if (xw.isfixed) {
		sizeh->flags |= PMaxSize;
		sizeh->min_width = sizeh->max_width = twin.w;
		sizeh->min_height = sizeh->max_height = twin.h;
	}
	if (xw.gm & (XValue|YValue)) {
		sizeh->flags |= USPosition | PWinGravity;
		sizeh->x = xw.l;
		sizeh->y = xw.t;
		sizeh->win_gravity = xgeommasktogravity(xw.gm);
	}

	XSetWMProperties(getDisplay(), xw.win, NULL, NULL, NULL, 0, sizeh, &wm, &clazz);
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
	default:
		return SouthEastGravity;
	}
}

int xloadfont(Font *f, FcPattern *pattern) {
	/*
	 * Manually configure instead of calling XftMatchFont
	 * so that we can use the configured pattern for
	 * "missing glyph" lookups.
	 */
	FcPattern *configured = FcPatternDuplicate(pattern);
	if (!configured)
		return 1;

	FcPatternGuard configured_guard(configured);
	FcConfigSubstitute(nullptr, configured, FcMatchPattern);
	XftDefaultSubstitute(getDisplay(), xw.scr, configured);

	FcResult result;
	FcPattern *match = FcFontMatch(nullptr, configured, &result);
	if (!match)
		return 1;

	FcPatternGuard match_guard(match);

	if (!(f->match = XftFontOpenPattern(getDisplay(), match)))
		return 1;

	// ownership will be transferred now
	configured_guard.disarm();
	match_guard.disarm();

	int wantattr, haveattr;

	if ((XftPatternGetInteger(pattern, "slant", 0, &wantattr) == XftResultMatch)) {
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

	XGlyphInfo extents;
	XftTextExtentsUtf8(getDisplay(), f->match,
		(const FcChar8 *) config::ASCII_PRINTABLE,
		config::ASCII_PRINTABLE_LEN, &extents);

	f->set = nullptr;
	f->pattern = configured;

	f->ascent = f->match->ascent;
	f->descent = f->match->descent;
	f->lbearing = 0;
	f->rbearing = f->match->max_advance_width;

	f->height = f->ascent + f->descent;
	f->width = (extents.xOff + config::ASCII_PRINTABLE_LEN - 1) / config::ASCII_PRINTABLE_LEN;

	return 0;
}

static bool xloadfonts(const char *fontstr, double fontsize) {
	FcPattern *pattern;

	if (fontstr[0] == '-')
		pattern = XftXlfdParse(fontstr, False, False);
	else
		pattern = FcNameParse((const FcChar8 *)fontstr);

	if (!pattern)
		return false;

	double fontval;

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
		return false;

	if (usedfontsize < 0) {
		FcPatternGetDouble(dc.font.match->pattern,
		                   FC_PIXEL_SIZE, 0, &fontval);
		usedfontsize = fontval;
		if (fontsize == 0)
			defaultfontsize = fontval;
	}

	/* Setting character width and height. */
	twin.cw = ceilf(dc.font.width * config::CWSCALE);
	twin.ch = ceilf(dc.font.height * config::CHSCALE);

	FcPatternDel(pattern, FC_SLANT);
	FcPatternAddInteger(pattern, FC_SLANT, FC_SLANT_ITALIC);
	if (xloadfont(&dc.ifont, pattern))
		return false;

	FcPatternDel(pattern, FC_WEIGHT);
	FcPatternAddInteger(pattern, FC_WEIGHT, FC_WEIGHT_BOLD);
	if (xloadfont(&dc.ibfont, pattern))
		return false;

	FcPatternDel(pattern, FC_SLANT);
	FcPatternAddInteger(pattern, FC_SLANT, FC_SLANT_ROMAN);
	if (xloadfont(&dc.bfont, pattern))
		return false;

	FcPatternDestroy(pattern);

	return true;
}

static void xloadfontsOrThrow(const char *fontstr, double fontsize) {
	if (!xloadfonts(fontstr, fontsize)) {
		cosmos_throw (cosmos::RuntimeError(cosmos::sprintf("failed to open font %s", usedfont)));
	}
}

void xunloadfont(Font *f) {
	XftFontClose(getDisplay(), f->match);
	FcPatternDestroy(f->pattern);
	if (f->set)
		FcFontSetDestroy(f->set);
}

void xunloadfonts() {
	/* Free the loaded fonts in the font cache.  */
	for (auto &fc: frc)
		XftFontClose(getDisplay(), fc.font);

	frc.clear();

	for (auto font: {&dc.font, &dc.bfont, &dc.ifont, &dc.ibfont}) {
		xunloadfont(font);
	}
}

int ximopen() {
	XIMCallback imdestroy = { .client_data = nullptr, .callback = ximdestroy };
	XICCallback icdestroy = { .client_data = nullptr, .callback = xicdestroy };

	xw.ime.xim = XOpenIM(getDisplay(), nullptr, nullptr, nullptr);
	if (xw.ime.xim == nullptr)
		return 0;

	if (XSetIMValues(xw.ime.xim, XNDestroyCallback, &imdestroy, nullptr))
		fprintf(stderr, "XSetIMValues: "
		                "Could not set XNDestroyCallback.\n");

	xw.ime.spotlist = XVaCreateNestedList(0, XNSpotLocation, &xw.ime.spot,
	                                      nullptr);

	if (xw.ime.xic == nullptr) {
		xw.ime.xic = XCreateIC(xw.ime.xim, XNInputStyle,
		                       XIMPreeditNothing | XIMStatusNothing,
		                       XNClientWindow, xw.win,
		                       XNDestroyCallback, &icdestroy,
		                       nullptr);
	}
	if (xw.ime.xic == nullptr)
		fprintf(stderr, "XCreateIC: Could not create input context.\n");

	return 1;
}

void ximinstantiate(Display *, XPointer, XPointer) {
	if (ximopen())
		XUnregisterIMInstantiateCallback(getDisplay(), NULL, NULL, NULL,
		                                 ximinstantiate, NULL);
}

void ximdestroy(XIM, XPointer, XPointer) {
	xw.ime.xim = nullptr;
	XRegisterIMInstantiateCallback(getDisplay(), nullptr, nullptr, nullptr,
	                               ximinstantiate, nullptr);
	XFree(xw.ime.spotlist);
}

int xicdestroy(XIC, XPointer, XPointer) {
	xw.ime.xic = nullptr;
	return 1;
}

void xinit() {
	XColor xmousefg, xmousebg;

	xw.display = &xpp::XDisplay::getInstance();
	xw.mapper = &xpp::XAtomMapper::getInstance();
	xw.scr = XDefaultScreen(getDisplay());
	xw.vis = XDefaultVisual(getDisplay(), xw.scr);

	/* font */
	if (!FcInit())
		cosmos_throw (cosmos::RuntimeError("could not init fontconfig"));

	usedfont = cmdline.font.getValue().c_str();
	xloadfontsOrThrow(usedfont, 0);

	/* colors */
	xw.cmap = XDefaultColormap(getDisplay(), xw.scr);
	xloadcols();

	/* adjust fixed window geometry */
	twin.w = 2 * config::BORDERPX + cols * twin.cw;
	twin.h = 2 * config::BORDERPX + rows * twin.ch;
	if (xw.gm & XNegative)
		xw.l += DisplayWidth(getDisplay(), xw.scr) - twin.w - 2;
	if (xw.gm & YNegative)
		xw.t += DisplayHeight(getDisplay(), xw.scr) - twin.h - 2;

	/* Events */
	xw.attrs.background_pixel = dc.col[config::DEFAULTBG].pixel;
	xw.attrs.border_pixel = dc.col[config::DEFAULTBG].pixel;
	xw.attrs.bit_gravity = NorthWestGravity;
	xw.attrs.event_mask = FocusChangeMask | KeyPressMask | KeyReleaseMask
		| ExposureMask | VisibilityChangeMask | StructureNotifyMask
		| ButtonMotionMask | ButtonPressMask | ButtonReleaseMask;
	xw.attrs.colormap = xw.cmap;

	Window parent;
	const auto &embed = cmdline.embed_window.getValue();
	if (!(!embed.empty() && (parent = strtol(embed.c_str(), nullptr, 0))))
		parent = XRootWindow(getDisplay(), xw.scr);
	xw.win = XCreateWindow(getDisplay(), parent, xw.l, xw.t,
			twin.w, twin.h, 0, XDefaultDepth(getDisplay(), xw.scr), InputOutput,
			xw.vis, CWBackPixel | CWBorderPixel | CWBitGravity
			| CWEventMask | CWColormap, &xw.attrs);

	XGCValues gcvalues = {};
	gcvalues.graphics_exposures = False;
	dc.gc = XCreateGC(getDisplay(), parent, GCGraphicsExposures, &gcvalues);
	xw.buf = XCreatePixmap(getDisplay(), xw.win, twin.w, twin.h, DefaultDepth(getDisplay(), xw.scr));
	XSetForeground(getDisplay(), dc.gc, dc.col[config::DEFAULTBG].pixel);
	XFillRectangle(getDisplay(), xw.buf, dc.gc, 0, 0, twin.w, twin.h);

	/* font spec buffer */
	xw.specbuf.resize(cols);

	/* Xft rendering context */
	xw.draw = XftDrawCreate(getDisplay(), xw.buf, xw.vis, xw.cmap);

	/* input methods */
	if (!ximopen()) {
		XRegisterIMInstantiateCallback(getDisplay(), NULL, NULL, NULL,
	                                       ximinstantiate, NULL);
	}

	/* white cursor, black outline */
	Cursor cursor = XCreateFontCursor(getDisplay(), config::MOUSESHAPE);
	XDefineCursor(getDisplay(), xw.win, cursor);

	if (XParseColor(getDisplay(), xw.cmap, config::colorname[config::MOUSEFG], &xmousefg) == 0) {
		xmousefg.red   = 0xffff;
		xmousefg.green = 0xffff;
		xmousefg.blue  = 0xffff;
	}

	if (XParseColor(getDisplay(), xw.cmap, config::colorname[config::MOUSEBG], &xmousebg) == 0) {
		xmousebg.red   = 0x0000;
		xmousebg.green = 0x0000;
		xmousebg.blue  = 0x0000;
	}

	XRecolorCursor(getDisplay(), cursor, &xmousefg, &xmousebg);

	xw.xembed = getAtom("_XEMBED");
	xw.wmdeletewin = getAtom("WM_DELETE_WINDOW");
	xw.netwmname = getAtom("_NET_WM_NAME");
	xw.netwmiconname = getAtom("_NET_WM_ICON_NAME");
	XSetWMProtocols(getDisplay(), xw.win, &xw.wmdeletewin, 1);

	xw.netwmpid = getAtom("_NET_WM_PID");
	auto thispid = cosmos::g_process.getPid();
	XChangeProperty(getDisplay(), xw.win, xw.netwmpid, XA_CARDINAL, 32,
			PropModeReplace, (uchar *)&thispid, 1);

	twin.mode = WinModeMask(WinMode::NUMLOCK);
	xsettitle(nullptr);
	xhints();
	XMapWindow(getDisplay(), xw.win);
	XSync(getDisplay(), False);

	xsel.tclick1.mark();
	xsel.tclick2.mark();
	xsel.primary.clear();
	xsel.clipboard.clear();
	xsel.xtarget = getAtom("UTF8_STRING");
	if (xsel.xtarget == None)
		xsel.xtarget = XA_STRING;
}

size_t xmakeglyphfontspecs(XftGlyphFontSpec *specs, const Glyph *glyphs,
		size_t len, int x, int y) {
	const float winx = config::BORDERPX + x * twin.cw, winy = config::BORDERPX + y * twin.ch;
	Font *fnt = &dc.font;
	FRC frcflags = FRC::NORMAL;
	float runewidth = twin.cw;
	size_t numspecs = 0;
	Glyph::AttrBitMask prevmode(Glyph::AttrBitMask::all);

	for (size_t i = 0, xp = winx, yp = winy + fnt->ascent; i < len; ++i) {
		/* Fetch rune and mode for current glyph. */
		Rune rune = glyphs[i].u;
		const auto &mode = glyphs[i].mode;

		/* Skip dummy wide-character spacing. */
		if (mode == Glyph::AttrBitMask({Attr::WDUMMY}))
			continue;

		/* Determine font for glyph if different from previous glyph. */
		if (prevmode != mode) {
			prevmode = mode;
			runewidth = twin.cw * (mode[Attr::WIDE] ? 2.0f : 1.0f);
			if (mode.allOf({Attr::ITALIC, Attr::BOLD})) {
				fnt = &dc.ibfont;
				frcflags = FRC::ITALICBOLD;
			} else if (mode[Attr::ITALIC]) {
				fnt = &dc.ifont;
				frcflags = FRC::ITALIC;
			} else if (mode[Attr::BOLD]) {
				fnt = &dc.bfont;
				frcflags = FRC::BOLD;
			} else {
				fnt = &dc.font;
				frcflags = FRC::NORMAL;
			}
			yp = winy + fnt->ascent;
		}

		/* Lookup character index with default font. */
		auto glyphidx = XftCharIndex(getDisplay(), fnt->match, rune);
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
			glyphidx = XftCharIndex(getDisplay(), fc.font, rune);
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
			FcResult fcres;
			if (!fnt->set)
				fnt->set = FcFontSort(0, fnt->pattern,
				                       1, 0, &fcres);
			FcFontSet *fcsets[] = { NULL };
			fcsets[0] = fnt->set;

			/*
			 * Nothing was found in the cache. Now use some dozen
			 * of Fontconfig calls to get the font for one single
			 * character.
			 *
			 * Xft and fontconfig are design failures.
			 */
			FcPattern *fcpattern = FcPatternDuplicate(fnt->pattern);
			FcPatternGuard fcpattern_guard(fcpattern);
			FcCharSet *fccharset = FcCharSetCreate();
			FcCharSetGuard fccharset_guard(fccharset);

			FcCharSetAddChar(fccharset, rune);
			FcPatternAddCharSet(fcpattern, FC_CHARSET, fccharset);
			FcPatternAddBool(fcpattern, FC_SCALABLE, 1);

			FcConfigSubstitute(0, fcpattern, FcMatchPattern);
			FcDefaultSubstitute(fcpattern);

			FcPattern *fontpattern = FcFontSetMatch(0, fcsets, 1, fcpattern, &fcres);

			/* Allocate memory for the new cache entry. */

			auto font = XftFontOpenPattern(getDisplay(), fontpattern);
			if (!font)
				cosmos_throw (cosmos::ApiError("XftFontOpenPattern failed seeking fallback font"));
			frc.emplace_back(Fontcache{font, frcflags, rune});

			glyphidx = XftCharIndex(getDisplay(), frc.back().font, rune);

			font_entry = &frc.back();
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

void xdrawglyphfontspecs(const XftGlyphFontSpec *specs, Glyph base, size_t len, int x, int y) {
	const size_t charlen = len * (base.mode[Attr::WIDE] ? 2 : 1);

	/* Fallback on color display for attributes not supported by the font */
	if (base.mode[Attr::ITALIC] && base.mode[Attr::BOLD]) {
		if (dc.ibfont.badslant || dc.ibfont.badweight)
			base.fg = config::DEFAULTATTR;
	} else if ((base.mode[Attr::ITALIC] && dc.ifont.badslant) ||
	    (base.mode[Attr::BOLD] && dc.bfont.badweight)) {
		base.fg = config::DEFAULTATTR;
	}

	Color *fg;
	XRenderColor colfg;
	Color truefg;

	if (base.isFgTrueColor()) {
		setRenderColor(colfg, base.fg);
		XftColorAllocValue(getDisplay(), xw.vis, xw.cmap, &colfg, &truefg);
		fg = &truefg;
	} else {
		fg = &dc.col[base.fg];
	}

	Color *bg;
	XRenderColor colbg;
	Color truebg;

	if (base.isBgTrueColor()) {
		setRenderColor(colbg, base.bg);
		XftColorAllocValue(getDisplay(), xw.vis, xw.cmap, &colbg, &truebg);
		bg = &truebg;
	} else {
		bg = &dc.col[base.bg];
	}

	/* Change basic system colors [0-7] to bright system colors [8-15] */
	if (base.mode[Attr::BOLD] && !base.mode[Attr::FAINT] && base.fg <= 7)
		fg = &dc.col[base.fg + 8];

	Color revfg, revbg;
	if (twin.mode[WinMode::REVERSE]) {
		if (fg == &dc.col[config::DEFAULTFG]) {
			fg = &dc.col[config::DEFAULTBG];
		} else {
			colfg.red = ~fg->color.red;
			colfg.green = ~fg->color.green;
			colfg.blue = ~fg->color.blue;
			colfg.alpha = fg->color.alpha;
			XftColorAllocValue(getDisplay(), xw.vis, xw.cmap, &colfg,
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
			XftColorAllocValue(getDisplay(), xw.vis, xw.cmap, &colbg,
					&revbg);
			bg = &revbg;
		}
	}

	if (base.mode[Attr::FAINT] && !base.mode[Attr::BOLD]) {
		colfg.red = fg->color.red / 2;
		colfg.green = fg->color.green / 2;
		colfg.blue = fg->color.blue / 2;
		colfg.alpha = fg->color.alpha;
		XftColorAllocValue(getDisplay(), xw.vis, xw.cmap, &colfg, &revfg);
		fg = &revfg;
	}

	if (base.mode[Attr::REVERSE]) {
		std::swap(fg, bg);
	}

	if (base.mode[Attr::BLINK] && twin.mode[WinMode::BLINK])
		fg = bg;

	if (base.mode[Attr::INVISIBLE])
		fg = bg;

	/* Intelligent cleaning up of the borders. */
	int winx = config::BORDERPX + x * twin.cw,
	    winy = config::BORDERPX + y * twin.ch,
	    width = charlen * twin.cw;

	if (x == 0) {
		xclear(0, (y == 0)? 0 : winy, config::BORDERPX,
			winy + twin.ch +
			((winy + twin.ch >= config::BORDERPX + twin.th)? twin.h : 0));
	}

	if (winx + width >= config::BORDERPX + twin.tw) {
		xclear(winx + width, (y == 0)? 0 : winy, twin.w,
			((winy + twin.ch >= config::BORDERPX + twin.th)? twin.h : (winy + twin.ch)));
	}
	if (y == 0)
		xclear(winx, 0, winx + width, config::BORDERPX);
	if (winy + twin.ch >= config::BORDERPX + twin.th)
		xclear(winx, winy + twin.ch, winx + width, twin.h);

	/* Clean up the region we want to draw to. */
	XftDrawRect(xw.draw, bg, winx, winy, width, twin.ch);

	/* Set the clip region because Xft is sometimes dirty. */
	XRectangle r;
	r.x = 0;
	r.y = 0;
	r.height = twin.ch;
	r.width = width;
	XftDrawSetClipRectangles(xw.draw, winx, winy, &r, 1);

	/* Render the glyphs. */
	XftDrawGlyphFontSpec(xw.draw, fg, specs, len);

	/* Render underline and strikethrough. */
	if (base.mode[Attr::UNDERLINE]) {
		XftDrawRect(xw.draw, fg, winx, winy + dc.font.ascent + 1,
				width, 1);
	}

	if (base.mode[Attr::STRUCK]) {
		XftDrawRect(xw.draw, fg, winx, winy + 2 * dc.font.ascent / 3,
				width, 1);
	}

	/* Reset clip to none. */
	XftDrawSetClip(xw.draw, 0);
}

void xdrawglyph(Glyph g, int x, int y) {
	XftGlyphFontSpec spec;

	auto numspecs = xmakeglyphfontspecs(&spec, &g, 1, x, y);
	xdrawglyphfontspecs(&spec, g, numspecs, x, y);
}

void xdrawcursor(int cx, int cy, Glyph g, int ox, int oy, Glyph og) {

	/* remove the old cursor */
	if (g_sel.isSelected(ox, oy))
		og.mode.flip(Attr::REVERSE);
	xdrawglyph(og, ox, oy);

	if (twin.mode[WinMode::HIDE])
		return;

	/*
	 * Select the right color for the right mode.
	 */
	g.mode.limit({Attr::BOLD, Attr::ITALIC, Attr::UNDERLINE, Attr::STRUCK, Attr::WIDE});
	Color drawcol;

	if (twin.mode[WinMode::REVERSE]) {
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
	if (twin.mode[WinMode::FOCUSED]) {
		switch (twin.cursor) {
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
					config::BORDERPX + cx * twin.cw,
					config::BORDERPX + (cy + 1) * twin.ch - \
						config::CURSORTHICKNESS,
					twin.cw, config::CURSORTHICKNESS);
			break;
		case 5: /* Blinking bar */
		case 6: /* Steady bar */
			XftDrawRect(xw.draw, &drawcol,
					config::BORDERPX + cx * twin.cw,
					config::BORDERPX + cy * twin.ch,
					config::CURSORTHICKNESS, twin.ch);
			break;
		}
	} else {
		XftDrawRect(xw.draw, &drawcol,
				config::BORDERPX + cx * twin.cw,
				config::BORDERPX + cy * twin.ch,
				twin.cw - 1, 1);
		XftDrawRect(xw.draw, &drawcol,
				config::BORDERPX + cx * twin.cw,
				config::BORDERPX + cy * twin.ch,
				1, twin.ch - 1);
		XftDrawRect(xw.draw, &drawcol,
				config::BORDERPX + (cx + 1) * twin.cw - 1,
				config::BORDERPX + cy * twin.ch,
				1, twin.ch - 1);
		XftDrawRect(xw.draw, &drawcol,
				config::BORDERPX + cx * twin.cw,
				config::BORDERPX + (cy + 1) * twin.ch - 1,
				twin.cw, 1);
	}
}

void xsetenv(void) {
	setenv("WINDOWID", std::to_string(xw.win).c_str(), 1);
}

void xseticontitle(const char *p) {
	XTextProperty prop;
	p = p ? p : cmdline.getTitle().c_str();

	if (Xutf8TextListToTextProperty(getDisplay(), (char**)&p, 1, XUTF8StringStyle,
	                                &prop) != Success)
		return;
	XSetWMIconName(getDisplay(), xw.win, &prop);
	XSetTextProperty(getDisplay(), xw.win, &prop, xw.netwmiconname);
	XFree(prop.value);
}

void xsettitle(const char *p) {
	XTextProperty prop;
	p = p ? p : cmdline.getTitle().c_str();

	if (Xutf8TextListToTextProperty(getDisplay(), (char**)&p, 1, XUTF8StringStyle,
	                                &prop) != Success)
		return;
	XSetWMName(getDisplay(), xw.win, &prop);
	XSetTextProperty(getDisplay(), xw.win, &prop, xw.netwmname);
	XFree(prop.value);
}

bool xstartdraw(void) {
	return twin.mode[WinMode::VISIBLE];
}

void xdrawline(const Line &line, int x1, int y1, int x2) {
	Glyph base, newone;
	XftGlyphFontSpec *specs = xw.specbuf.data();
	size_t i = 0;
	int ox = 0;

	auto numspecs = xmakeglyphfontspecs(specs, &line[x1], x2 - x1, x1, y1);
	for (int x = x1; x < x2 && i < numspecs; x++) {
		newone = line[x];
		if (newone.mode.only(Attr::WDUMMY))
			continue;
		if (g_sel.isSelected(x, y1))
			newone.mode.flip(Attr::REVERSE);
		if (i > 0 && base.attrsDiffer(newone)) {
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

void xfinishdraw() {
	XCopyArea(getDisplay(), xw.buf, xw.win, dc.gc, 0, 0, twin.w, twin.h, 0, 0);
	XSetForeground(getDisplay(), dc.gc,
			dc.col[twin.mode[WinMode::REVERSE]?
				config::DEFAULTFG : config::DEFAULTBG].pixel);
}

void xximspot(int x, int y) {
	if (xw.ime.xic == nullptr)
		return;

	xw.ime.spot.x = config::BORDERPX + x * twin.cw;
	xw.ime.spot.y = config::BORDERPX + (y + 1) * twin.ch;

	XSetICValues(xw.ime.xic, XNPreeditAttributes, xw.ime.spotlist, nullptr);
}

void expose(XEvent *) {
	term.redraw();
}

void visibility(XEvent *ev) {
	XVisibilityEvent *e = &ev->xvisibility;

	twin.mode.set(WinMode::VISIBLE, e->state != VisibilityFullyObscured);
}

void unmap(XEvent *) {
	twin.mode.reset(WinMode::VISIBLE);
}

void xsetpointermotion(int set) {
	modifyBit(xw.attrs.event_mask, set, PointerMotionMask);
	XChangeWindowAttributes(getDisplay(), xw.win, CWEventMask, &xw.attrs);
}

void xsetmode(bool set, const WinMode &flag) {
	auto mode = twin.mode;
	twin.mode.set(flag, set);
	if (twin.mode[WinMode::REVERSE] != mode[WinMode::REVERSE])
		term.redraw();
}

int xsetcursor(int cursor) {
	if (!cosmos::in_range(cursor, 0, 7)) /* 7: st extension */
		return 1;
	twin.cursor = cursor;
	return 0;
}

void xseturgency(int add) {
	XWMHints *h = XGetWMHints(getDisplay(), xw.win);

	modifyBit(h->flags, add, XUrgencyHint);
	XSetWMHints(getDisplay(), xw.win, h);
	XFree(h);
}

void xbell(void) {
	if (!(twin.mode[WinMode::FOCUSED]))
		xseturgency(1);
	if (config::BELLVOLUME)
		XkbBell(getDisplay(), xw.win, config::BELLVOLUME, (Atom)NULL);
}

void focus(XEvent *ev) {
	XFocusChangeEvent *e = &ev->xfocus;

	if (e->mode == NotifyGrab)
		return;

	if (ev->type == FocusIn) {
		if (xw.ime.xic)
			XSetICFocus(xw.ime.xic);
		twin.mode.set(WinMode::FOCUSED);
		xseturgency(0);
		if (twin.mode[WinMode::FOCUS])
			g_tty.write("\033[I", 3, 0);
	} else {
		if (xw.ime.xic)
			XUnsetICFocus(xw.ime.xic);
		twin.mode.reset(WinMode::FOCUSED);
		if (twin.mode[WinMode::FOCUS])
			g_tty.write("\033[O", 3, 0);
	}
}

bool match(uint mask, uint state) {
	return mask == XK_ANY_MOD || mask == (state & ~config::IGNOREMOD);
}

const char* kmap(KeySym k, uint state) {
	bool found = false;

	/* Check for mapped keys out of X11 function keys. */
	for (auto &key: config::MAPPEDKEYS) {
		if (key == k) {
			found = true;
			break;
		}
	}
	if (!found) {
		if ((k & 0xFFFF) < 0xFD00)
			return nullptr;
	}

	for (auto &key: config::KEY) {
		if (key.k != k)
			continue;

		if (!match(key.mask, state))
			continue;

		if (twin.mode[WinMode::APPKEYPAD] ? key.appkey < 0 : key.appkey > 0)
			continue;
		if (twin.mode[WinMode::NUMLOCK] && key.appkey == 2)
			continue;

		if (twin.mode[WinMode::APPCURSOR] ? key.appcursor < 0 : key.appcursor > 0)
			continue;

		return key.s;
	}

	return nullptr;
}

void kpress(XEvent *ev) {
	XKeyEvent *e = &ev->xkey;
	KeySym ksym;
	char buf[64];
	int len;
	Status status;

	if (twin.mode[WinMode::KBDLOCK])
		return;

	if (xw.ime.xic)
		len = XmbLookupString(xw.ime.xic, e, buf, sizeof(buf), &ksym, &status);
	else
		len = XLookupString(e, buf, sizeof(buf), &ksym, NULL);

	/* 1. shortcuts */
	for (auto &sc: config::SHORTCUTS) {
		if (ksym == sc.keysym && match(sc.mod, e->state)) {
			sc.func(&(sc.arg));
			return;
		}
	}

	/* 2. custom keys from nst_config.h */
	if (const char *customkey = nullptr; (customkey = kmap(ksym, e->state))) {
		g_tty.write(customkey, strlen(customkey), 1);
		return;
	}

	/* 3. composed string from input method */
	if (len == 0)
		return;
	else if (len == 1 && e->state & Mod1Mask) {
		if (twin.mode[WinMode::EIGHT_BIT]) {
			if (*buf < 0177) {
				Rune c = *buf | 0x80;
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
			twin.mode.set(WinMode::FOCUSED);
			xseturgency(0);
		} else if (e->xclient.data.l[1] == XEMBED_FOCUS_OUT) {
			twin.mode.reset(WinMode::FOCUSED);
		}
	} else if ((Atom)e->xclient.data.l[0] == xw.wmdeletewin) {
		g_tty.hangup();
		exit(0);
	}
}

void resize(XEvent *e) {
	if (e->xconfigure.width == twin.w && e->xconfigure.height == twin.h)
		return;

	cresize(e->xconfigure.width, e->xconfigure.height);
}

void waitForWindowMapping() {
	XEvent ev;
	int w = twin.w, h = twin.h;

	/* Waiting for window mapping */
	do {
		XNextEvent(getDisplay(), &ev);
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
	auto xfd = cosmos::FileDescriptor(XConnectionNumber(getDisplay()));
	cosmos::Poller poller;

	poller.create();
	for (auto fd: {ttyfd, xfd, childfd}) {
		poller.addFD(fd, cosmos::Poller::MonitorMask({cosmos::Poller::MonitorSetting::INPUT}));
	}

	XEvent ev;
	bool drawing = false;
	cosmos::MonotonicStopWatch draw_watch, blink_watch(cosmos::MonotonicStopWatch::InitialMark(true));
	std::chrono::milliseconds timeout(-1);

	while (true) {
		if (XPending(getDisplay()))
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

		while (XPending(getDisplay())) {
			x_ev = true;
			XNextEvent(getDisplay(), &ev);
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
					twin.mode.set(WinMode::BLINK);
				twin.mode.flip(WinMode::BLINK);
				term.setDirtyByAttr(Attr::BLINK);
				blink_watch.mark();
				timeout = config::BLINKTIMEOUT;
			}
		}

		term.draw();
		XFlush(getDisplay());
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

	if (cmd.fixed_geometry.isSet()) {
		xw.isfixed = true;
	}

	if (cmd.window_geometry.isSet()) {
		xw.gm = XParseGeometry(
			cmd.window_geometry.getValue().c_str(),
			&xw.l, &xw.t, &cols, &rows
		);
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
	xinit();
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
