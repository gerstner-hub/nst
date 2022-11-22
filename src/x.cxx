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
#include "X++/Event.hxx"
#include "X++/RootWin.hxx"
#include "X++/XAtom.hxx"
#include "X++/XDisplay.hxx"
#include "X++/Xpp.hxx"
#include "X++/XWindow.hxx"

// nst
/* nst_config.h for applying patches and the configuration. */
#include "nst_config.h"
#include "Cmdline.hxx"
#include "codecs.hxx"
#include "font.hxx"
#include "helper.hxx"
#include "nst.hxx"
#include "types.hxx"
#include "Selection.hxx"
#include "Term.hxx"
#include "TTY.hxx"
#include "win.h"
#include "xtypes.hxx"
#include "x.hxx"

// config parts specific to this compilation unit
#include "nst_config.inl"

namespace nst {

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
static void xloadfontsOrThrow(const std::string&, double fontsize=0);

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
X11 x11;
XSelection xsel;
TermWindow twin;

/* Fontcache is an array now. A new font will be appended to the array. */
std::vector<Fontcache> frc;
double usedfontsize = 0;
double defaultfontsize = 0;

Cmdline cmdline;

PressedButtons buttons; /* bit field of pressed buttons */

unsigned int cols = config::COLS;
unsigned int rows = config::ROWS;

inline Display* getDisplay() {
	return static_cast<Display*>(*x11.display);
}

inline Atom getAtom(const char *name) {
	return (x11.mapper)->getAtom(name);
}

} // end anon ns

Nst *Nst::the_instance = nullptr;

void clipcopy(const Arg *) {
	xsel.clipboard.clear();

	if (!xsel.primary.empty()) {
		xsel.clipboard = xsel.primary;
		Atom clipboard = getAtom("CLIPBOARD");
		XSetSelectionOwner(getDisplay(), clipboard, x11.win, CurrentTime);
	}
}

void clippaste(const Arg *) {
	Atom clipboard = getAtom("CLIPBOARD");
	XConvertSelection(getDisplay(), clipboard, xsel.xtarget, clipboard,
			x11.win, CurrentTime);
}

void selpaste(const Arg *) {
	XConvertSelection(getDisplay(), XA_PRIMARY, xsel.xtarget, XA_PRIMARY,
			x11.win, CurrentTime);
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
	xloadfontsOrThrow(cmdline.font.getValue(), arg->f);
	cresize(0, 0);
	Nst::getTerm().redraw();
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
	Nst::getTTY().write(arg->s, strlen(arg->s), 1);
}

void toggleprinter(const Arg *) {
	auto &term = Nst::getTerm();
	term.setPrintMode(!term.isPrintMode());
}

void printscreen(const Arg *) {
	Nst::getTerm().dump();
}

void printsel(const Arg *) {
	Nst::getSelection().dump();
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
	auto &sel = Nst::getSelection();
	sel.extend(evcol(e), evrow(e), seltype, done);
	if (done) {
		setsel(sel.getSelection(), e->xbutton.time);
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

	Nst::getTTY().write(buf, len, false);
}

const char* getColorName(size_t nr) {
	if (nr < config::COLORNAMES.size())
		return config::COLORNAMES[nr];
	else if (nr < 256)
		// unassigned
		return nullptr;
	else {
		// check for extended colors
		nr -= 256;
		if (nr < config::EXTENDED_COLORS.size())
			return config::EXTENDED_COLORS[nr];
	}

	return nullptr;
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

		Nst::getSelection().start(evcol(e), evrow(e), snap);
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
	auto &tty = Nst::getTTY();

	do {
		if (XGetWindowProperty(getDisplay(), x11.win, property, ofs,
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
			modifyBit(x11.attrs.event_mask, 0, PropertyChangeMask);
			XChangeWindowAttributes(getDisplay(), x11.win, CWEventMask,
					&x11.attrs);
		}

		if (type == incratom) {
			/*
			 * Activate the PropertyNotify events so we receive
			 * when the selection owner does send us the next
			 * chunk of data.
			 */
			modifyBit(x11.attrs.event_mask, 1, PropertyChangeMask);
			XChangeWindowAttributes(getDisplay(), x11.win, CWEventMask,
					&x11.attrs);

			/*
			 * Deleting the property is the transfer start signal.
			 */
			XDeleteProperty(getDisplay(), x11.win, (int)property);
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
			tty.write("\033[200~", 6, 0);
		tty.write((char *)data, nitems * format / 8, 1);
		if (twin.mode[WinMode::BRCKTPASTE] && rem == 0)
			tty.write("\033[201~", 6, 0);
		XFree(data);
		/* number of 32-bit chunks returned */
		ofs += nitems * format / 32;
	} while (rem > 0);

	/*
	 * Deleting the property again tells the selection owner to send the
	 * next data chunk in the property.
	 */
	XDeleteProperty(getDisplay(), x11.win, (int)property);
}

void xclipcopy(void) {
	clipcopy(nullptr);
}

[[maybe_unused]]
void selclear_(XEvent *) {
	Nst::getSelection().clear();
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

	XSetSelectionOwner(getDisplay(), XA_PRIMARY, x11.win, t);
	if (XGetSelectionOwner(getDisplay(), XA_PRIMARY) != x11.win)
		Nst::getSelection().clear();
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

	Nst::getTerm().resize(col, row);
	xresize(col, row);
	Nst::getTTY().resize(twin.tw, twin.th);
}

void xresize(int col, int row) {
	twin.tw = col * twin.cw;
	twin.th = row * twin.ch;

	XFreePixmap(getDisplay(), x11.buf);
	x11.buf = XCreatePixmap(getDisplay(), x11.win, twin.w, twin.h,
			DefaultDepth(getDisplay(), x11.scr));
	XftDrawChange(x11.draw, x11.buf);
	xclear(0, 0, twin.w, twin.h);

	/* resize to new width */
	x11.specbuf.resize(col);
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
			return XftColorAllocValue(getDisplay(), x11.vis,
			                          x11.cmap, &color, ncolor);
		} else
			name = getColorName(i);
	}

	return XftColorAllocName(getDisplay(), x11.vis, x11.cmap, name, ncolor);
}

void xloadcols(void) {
	static bool loaded;

	if (loaded) {
		for (auto &c: dc.col) {
			XftColorFree(getDisplay(), x11.vis, x11.cmap, &c);
		}
	} else {

		auto len = std::max(256UL + config::EXTENDED_COLORS.size(), 256UL);
		dc.col.resize(len);
	}

	for (size_t i = 0; i < dc.col.size(); i++) {
		if (!xloadcolor(i, nullptr, &dc.col[i])) {
			auto colorname = getColorName(i);
			if (colorname)
				cosmos_throw (cosmos::ApiError(cosmos::sprintf("could not allocate color '%s'", colorname)));
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

	XftColorFree(getDisplay(), x11.vis, x11.cmap, &dc.col[x]);
	dc.col[x] = ncolor;

	return 0;
}

/*
 * Absolute coordinates.
 */
void xclear(int x1, int y1, int x2, int y2) {
	const auto colindex = twin.mode[WinMode::REVERSE] ? config::DEFAULTFG : config::DEFAULTBG;
	XftDrawRect(x11.draw, &dc.col[colindex], x1, y1, x2-x1, y2-y1);
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
	if (x11.isfixed) {
		sizeh->flags |= PMaxSize;
		sizeh->min_width = sizeh->max_width = twin.w;
		sizeh->min_height = sizeh->max_height = twin.h;
	}
	if (x11.gm & (XValue|YValue)) {
		sizeh->flags |= USPosition | PWinGravity;
		sizeh->x = x11.l;
		sizeh->y = x11.t;
		sizeh->win_gravity = xgeommasktogravity(x11.gm);
	}

	XSetWMProperties(getDisplay(), x11.win, NULL, NULL, NULL, 0, sizeh, &wm, &clazz);
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
	XftDefaultSubstitute(getDisplay(), x11.scr, configured);

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

static bool xloadfonts(const std::string &fontstr, double fontsize) {
	FontPattern pattern(fontstr);

	if (!pattern.isValid())
		return false;

	if (fontsize > 1) {
		pattern.setPixelSize(fontsize);
		usedfontsize = fontsize;
	} else {
		if (auto pxsize = pattern.getPixelSize(); pxsize.has_value())
			usedfontsize = *pxsize;
		else if(auto ptsize = pattern.getPointSize(); ptsize.has_value())
			usedfontsize = -1;
		else {
			/*
			 * Use default font size, if none given. This is to
			 * have a known usedfontsize value.
			 */
			usedfontsize = config::FONT_DEFAULT_SIZE_PX;
			pattern.setPixelSize(usedfontsize);
		}
		defaultfontsize = usedfontsize;
	}

	if (xloadfont(&dc.font, pattern.raw()))
		return false;

	if (usedfontsize < 0) {
		auto loaded = FontPattern(dc.font.match->pattern);
		if (auto pxsize = loaded.getPixelSize(); pxsize.has_value()) {
			usedfontsize = *pxsize;
			if (fontsize == 0)
				defaultfontsize = *pxsize;
		}
	}

	/* Setting character width and height. */
	twin.cw = ceilf(dc.font.width * config::CWSCALE);
	twin.ch = ceilf(dc.font.height * config::CHSCALE);

	pattern.setSlant(Slant::ITALIC);
	if (xloadfont(&dc.ifont, pattern.raw()))
		return false;

	pattern.setWeight(Weight::BOLD);
	if (xloadfont(&dc.ibfont, pattern.raw()))
		return false;

	pattern.setSlant(Slant::ROMAN);
	if (xloadfont(&dc.bfont, pattern.raw()))
		return false;

	return true;
}

static void xloadfontsOrThrow(const std::string &fontstr, double fontsize) {
	if (!xloadfonts(fontstr, fontsize)) {
		cosmos_throw (cosmos::RuntimeError(cosmos::sprintf("failed to open font %s", fontstr.c_str())));
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

	x11.ime.xim = XOpenIM(getDisplay(), nullptr, nullptr, nullptr);
	if (x11.ime.xim == nullptr)
		return 0;

	if (XSetIMValues(x11.ime.xim, XNDestroyCallback, &imdestroy, nullptr))
		fprintf(stderr, "XSetIMValues: "
		                "Could not set XNDestroyCallback.\n");

	x11.ime.spotlist = XVaCreateNestedList(0, XNSpotLocation, &x11.ime.spot,
	                                      nullptr);

	if (x11.ime.xic == nullptr) {
		// NOTE: this function takes varargs, passing in C++ objects
		// like x11.win even with conversion operator does not work
		x11.ime.xic = XCreateIC(x11.ime.xim, XNInputStyle,
		                       XIMPreeditNothing | XIMStatusNothing,
		                       XNClientWindow, x11.win.id(),
		                       XNDestroyCallback, &icdestroy,
		                       nullptr);
	}
	if (x11.ime.xic == nullptr)
		fprintf(stderr, "XCreateIC: Could not create input context.\n");

	return 1;
}

void ximinstantiate(Display *, XPointer, XPointer) {
	if (ximopen())
		XUnregisterIMInstantiateCallback(getDisplay(), NULL, NULL, NULL,
		                                 ximinstantiate, NULL);
}

void ximdestroy(XIM, XPointer, XPointer) {
	x11.ime.xim = nullptr;
	XRegisterIMInstantiateCallback(getDisplay(), nullptr, nullptr, nullptr,
	                               ximinstantiate, nullptr);
	XFree(x11.ime.spotlist);
}

int xicdestroy(XIC, XPointer, XPointer) {
	x11.ime.xic = nullptr;
	return 1;
}

void xinit() {
	XColor xmousefg, xmousebg;

	auto &display = xpp::XDisplay::getInstance();
	x11.display = &display;
	x11.mapper = &xpp::XAtomMapper::getInstance();
	x11.scr = display.getDefaultScreen();
	x11.vis = display.getDefaultVisual(x11.scr);

	/* font */
	if (!FcInit())
		cosmos_throw (cosmos::RuntimeError("could not init fontconfig"));

	xloadfontsOrThrow(cmdline.font.getValue());

	/* colors */
	x11.cmap = display.getDefaultColormap(x11.scr);
	xloadcols();

	/* adjust fixed window geometry */
	twin.w = 2 * config::BORDERPX + cols * twin.cw;
	twin.h = 2 * config::BORDERPX + rows * twin.ch;
	if (x11.gm & XNegative)
		x11.l += DisplayWidth(getDisplay(), x11.scr) - twin.w - 2;
	if (x11.gm & YNegative)
		x11.t += DisplayHeight(getDisplay(), x11.scr) - twin.h - 2;

	/* Events */
	x11.attrs.background_pixel = dc.col[config::DEFAULTBG].pixel;
	x11.attrs.border_pixel = dc.col[config::DEFAULTBG].pixel;
	x11.attrs.bit_gravity = NorthWestGravity;
	x11.attrs.event_mask = FocusChangeMask | KeyPressMask | KeyReleaseMask
		| ExposureMask | VisibilityChangeMask | StructureNotifyMask
		| ButtonMotionMask | ButtonPressMask | ButtonReleaseMask;
	x11.attrs.colormap = x11.cmap;

	std::optional<xpp::XWindow> parent;
	if (cmdline.embed_window.isSet()) {
		// use window ID passed on command line as parent
		parent.emplace(xpp::XWindow(cmdline.embed_window.getValue()));
	}

	if (!parent) {
		// either not embedded or the command line parsing failed
		parent.emplace(xpp::RootWin(display, x11.scr));
	}

	x11.win = display.createWindow(
		xpp::WindowSpec{x11.l, x11.t, static_cast<unsigned int>(twin.w), static_cast<unsigned int>(twin.h)},
		0,
		/*clazz = */InputOutput,
		&(*parent),
		display.getDefaultDepth(x11.scr),
		x11.vis,
		CWBackPixel | CWBorderPixel | CWBitGravity | CWEventMask | CWColormap,
		&x11.attrs
	);

	XGCValues gcvalues = {};
	gcvalues.graphics_exposures = False;
	dc.gc = XCreateGC(getDisplay(), parent->id(), GCGraphicsExposures, &gcvalues);
	x11.buf = XCreatePixmap(getDisplay(), x11.win, twin.w, twin.h, DefaultDepth(getDisplay(), x11.scr));
	XSetForeground(getDisplay(), dc.gc, dc.col[config::DEFAULTBG].pixel);
	XFillRectangle(getDisplay(), x11.buf, dc.gc, 0, 0, twin.w, twin.h);

	/* font spec buffer */
	x11.specbuf.resize(cols);

	/* Xft rendering context */
	x11.draw = XftDrawCreate(getDisplay(), x11.buf, x11.vis, x11.cmap);

	/* input methods */
	if (!ximopen()) {
		XRegisterIMInstantiateCallback(getDisplay(), NULL, NULL, NULL,
	                                       ximinstantiate, NULL);
	}

	/* white cursor, black outline */
	Cursor cursor = XCreateFontCursor(getDisplay(), config::MOUSESHAPE);
	XDefineCursor(getDisplay(), x11.win, cursor);

	if (XParseColor(getDisplay(), x11.cmap, getColorName(config::MOUSEFG), &xmousefg) == 0) {
		xmousefg.red   = 0xffff;
		xmousefg.green = 0xffff;
		xmousefg.blue  = 0xffff;
	}

	if (XParseColor(getDisplay(), x11.cmap, getColorName(config::MOUSEBG), &xmousebg) == 0) {
		xmousebg.red   = 0x0000;
		xmousebg.green = 0x0000;
		xmousebg.blue  = 0x0000;
	}

	XRecolorCursor(getDisplay(), cursor, &xmousefg, &xmousebg);

	x11.xembed = getAtom("_XEMBED");
	x11.wmdeletewin = getAtom("WM_DELETE_WINDOW");
	x11.netwmname = getAtom("_NET_WM_NAME");
	x11.netwmiconname = getAtom("_NET_WM_ICON_NAME");
	XSetWMProtocols(getDisplay(), x11.win, &x11.wmdeletewin, 1);

	x11.netwmpid = getAtom("_NET_WM_PID");
	auto thispid = cosmos::g_process.getPid();
	XChangeProperty(getDisplay(), x11.win, x11.netwmpid, XA_CARDINAL, 32,
			PropModeReplace, (uchar *)&thispid, 1);

	twin.mode = WinModeMask(WinMode::NUMLOCK);
	xsettitle(nullptr);
	xhints();
	XMapWindow(getDisplay(), x11.win);
	XSync(getDisplay(), False);

	xsel.tclick1.mark();
	xsel.tclick2.mark();
	xsel.primary.clear();
	xsel.clipboard.clear();
	xsel.xtarget = getAtom("UTF8_STRING");
	if (xsel.xtarget == None)
		xsel.xtarget = XA_STRING;

	if (getenv("NST_XSYNC") != nullptr) {
		::XSynchronize(display, true);
	}
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
		XftColorAllocValue(getDisplay(), x11.vis, x11.cmap, &colfg, &truefg);
		fg = &truefg;
	} else {
		fg = &dc.col[base.fg];
	}

	Color *bg;
	XRenderColor colbg;
	Color truebg;

	if (base.isBgTrueColor()) {
		setRenderColor(colbg, base.bg);
		XftColorAllocValue(getDisplay(), x11.vis, x11.cmap, &colbg, &truebg);
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
			XftColorAllocValue(getDisplay(), x11.vis, x11.cmap, &colfg,
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
			XftColorAllocValue(getDisplay(), x11.vis, x11.cmap, &colbg,
					&revbg);
			bg = &revbg;
		}
	}

	if (base.mode[Attr::FAINT] && !base.mode[Attr::BOLD]) {
		colfg.red = fg->color.red / 2;
		colfg.green = fg->color.green / 2;
		colfg.blue = fg->color.blue / 2;
		colfg.alpha = fg->color.alpha;
		XftColorAllocValue(getDisplay(), x11.vis, x11.cmap, &colfg, &revfg);
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
	XftDrawRect(x11.draw, bg, winx, winy, width, twin.ch);

	/* Set the clip region because Xft is sometimes dirty. */
	XRectangle r;
	r.x = 0;
	r.y = 0;
	r.height = twin.ch;
	r.width = width;
	XftDrawSetClipRectangles(x11.draw, winx, winy, &r, 1);

	/* Render the glyphs. */
	XftDrawGlyphFontSpec(x11.draw, fg, specs, len);

	/* Render underline and strikethrough. */
	if (base.mode[Attr::UNDERLINE]) {
		XftDrawRect(x11.draw, fg, winx, winy + dc.font.ascent + 1,
				width, 1);
	}

	if (base.mode[Attr::STRUCK]) {
		XftDrawRect(x11.draw, fg, winx, winy + 2 * dc.font.ascent / 3,
				width, 1);
	}

	/* Reset clip to none. */
	XftDrawSetClip(x11.draw, 0);
}

void xdrawglyph(Glyph g, int x, int y) {
	XftGlyphFontSpec spec;

	auto numspecs = xmakeglyphfontspecs(&spec, &g, 1, x, y);
	xdrawglyphfontspecs(&spec, g, numspecs, x, y);
}

void xdrawcursor(int cx, int cy, Glyph g, int ox, int oy, Glyph og) {

	auto &sel = Nst::getSelection();

	/* remove the old cursor */
	if (sel.isSelected(ox, oy))
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
		if (sel.isSelected(cx, cy)) {
			drawcol = dc.col[config::DEFAULTCS];
			g.fg = config::DEFAULTRCS;
		} else {
			drawcol = dc.col[config::DEFAULTRCS];
			g.fg = config::DEFAULTCS;
		}
	} else {
		if (sel.isSelected(cx, cy)) {
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
		case CursorStyle::SNOWMAN: /* st extension */
			g.u = 0x2603; /* snowman (U+2603) */
			/* FALLTHROUGH */
		case CursorStyle::BLINKING_BLOCK:
		case CursorStyle::BLINKING_BLOCK_DEFAULT:
		case CursorStyle::STEADY_BLOCK:
			xdrawglyph(g, cx, cy);
			break;
		case CursorStyle::BLINKING_UNDERLINE:
		case CursorStyle::STEADY_UNDERLINE:
			XftDrawRect(x11.draw, &drawcol,
					config::BORDERPX + cx * twin.cw,
					config::BORDERPX + (cy + 1) * twin.ch - \
						config::CURSORTHICKNESS,
					twin.cw, config::CURSORTHICKNESS);
			break;
		case CursorStyle::BLINKING_BAR:
		case CursorStyle::STEADY_BAR:
			XftDrawRect(x11.draw, &drawcol,
					config::BORDERPX + cx * twin.cw,
					config::BORDERPX + cy * twin.ch,
					config::CURSORTHICKNESS, twin.ch);
			break;
		default:
			break;
		}
	} else {
		XftDrawRect(x11.draw, &drawcol,
				config::BORDERPX + cx * twin.cw,
				config::BORDERPX + cy * twin.ch,
				twin.cw - 1, 1);
		XftDrawRect(x11.draw, &drawcol,
				config::BORDERPX + cx * twin.cw,
				config::BORDERPX + cy * twin.ch,
				1, twin.ch - 1);
		XftDrawRect(x11.draw, &drawcol,
				config::BORDERPX + (cx + 1) * twin.cw - 1,
				config::BORDERPX + cy * twin.ch,
				1, twin.ch - 1);
		XftDrawRect(x11.draw, &drawcol,
				config::BORDERPX + cx * twin.cw,
				config::BORDERPX + (cy + 1) * twin.ch - 1,
				twin.cw, 1);
	}
}

void xsetenv(void) {
	setenv("WINDOWID", std::to_string(x11.win).c_str(), 1);
}

void xseticontitle(const char *p) {
	XTextProperty prop;
	p = p ? p : cmdline.getTitle().c_str();

	if (Xutf8TextListToTextProperty(getDisplay(), (char**)&p, 1, XUTF8StringStyle,
	                                &prop) != Success)
		return;
	XSetWMIconName(getDisplay(), x11.win, &prop);
	XSetTextProperty(getDisplay(), x11.win, &prop, x11.netwmiconname);
	XFree(prop.value);
}

void xsettitle(const char *p) {
	XTextProperty prop;
	p = p ? p : cmdline.getTitle().c_str();

	if (Xutf8TextListToTextProperty(getDisplay(), (char**)&p, 1, XUTF8StringStyle,
	                                &prop) != Success)
		return;
	XSetWMName(getDisplay(), x11.win, &prop);
	XSetTextProperty(getDisplay(), x11.win, &prop, x11.netwmname);
	XFree(prop.value);
}

bool xstartdraw(void) {
	return twin.mode[WinMode::VISIBLE];
}

void xdrawline(const Line &line, int x1, int y1, int x2) {
	Glyph base, newone;
	XftGlyphFontSpec *specs = x11.specbuf.data();
	size_t i = 0;
	int ox = 0;

	auto numspecs = xmakeglyphfontspecs(specs, &line[x1], x2 - x1, x1, y1);
	for (int x = x1; x < x2 && i < numspecs; x++) {
		newone = line[x];
		if (newone.mode.only(Attr::WDUMMY))
			continue;
		if (Nst::getSelection().isSelected(x, y1))
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
	XCopyArea(getDisplay(), x11.buf, x11.win, dc.gc, 0, 0, twin.w, twin.h, 0, 0);
	XSetForeground(getDisplay(), dc.gc,
			dc.col[twin.mode[WinMode::REVERSE]?
				config::DEFAULTFG : config::DEFAULTBG].pixel);
}

void xximspot(int x, int y) {
	if (x11.ime.xic == nullptr)
		return;

	x11.ime.spot.x = config::BORDERPX + x * twin.cw;
	x11.ime.spot.y = config::BORDERPX + (y + 1) * twin.ch;

	XSetICValues(x11.ime.xic, XNPreeditAttributes, x11.ime.spotlist, nullptr);
}

void expose(XEvent *) {
	Nst::getTerm().redraw();
}

void visibility(XEvent *ev) {
	XVisibilityEvent *e = &ev->xvisibility;

	twin.mode.set(WinMode::VISIBLE, e->state != VisibilityFullyObscured);
}

void unmap(XEvent *) {
	twin.mode.reset(WinMode::VISIBLE);
}

void xsetpointermotion(int set) {
	modifyBit(x11.attrs.event_mask, set, PointerMotionMask);
	XChangeWindowAttributes(getDisplay(), x11.win, CWEventMask, &x11.attrs);
}

void xsetmode(bool set, const WinMode &flag) {
	auto mode = twin.mode;
	twin.mode.set(flag, set);
	if (twin.mode[WinMode::REVERSE] != mode[WinMode::REVERSE])
		Nst::getTerm().redraw();
}

void xsetcursor(const CursorStyle &cursor) {
	twin.cursor = cursor;
}

void xseturgency(int add) {
	XWMHints *h = XGetWMHints(getDisplay(), x11.win);

	modifyBit(h->flags, add, XUrgencyHint);
	XSetWMHints(getDisplay(), x11.win, h);
	XFree(h);
}

void xbell(void) {
	if (!(twin.mode[WinMode::FOCUSED]))
		xseturgency(1);
	if (config::BELLVOLUME)
		XkbBell(getDisplay(), x11.win, config::BELLVOLUME, (Atom)NULL);
}

void focus(XEvent *ev) {
	XFocusChangeEvent *e = &ev->xfocus;

	if (e->mode == NotifyGrab)
		return;

	if (ev->type == FocusIn) {
		if (x11.ime.xic)
			XSetICFocus(x11.ime.xic);
		twin.mode.set(WinMode::FOCUSED);
		xseturgency(0);
		if (twin.mode[WinMode::FOCUS])
			Nst::getTTY().write("\033[I", 3, 0);
	} else {
		if (x11.ime.xic)
			XUnsetICFocus(x11.ime.xic);
		twin.mode.reset(WinMode::FOCUSED);
		if (twin.mode[WinMode::FOCUS])
			Nst::getTTY().write("\033[O", 3, 0);
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

	if (x11.ime.xic)
		len = XmbLookupString(x11.ime.xic, e, buf, sizeof(buf), &ksym, &status);
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
		Nst::getTTY().write(customkey, strlen(customkey), 1);
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

	Nst::getTTY().write(buf, len, 1);
}

void cmessage(XEvent *e) {
	/*
	 * See xembed specs
	 *  http://standards.freedesktop.org/xembed-spec/xembed-spec-latest.html
	 */
	if (e->xclient.message_type == x11.xembed && e->xclient.format == 32) {
		if (e->xclient.data.l[1] == XEMBED_FOCUS_IN) {
			twin.mode.set(WinMode::FOCUSED);
			xseturgency(0);
		} else if (e->xclient.data.l[1] == XEMBED_FOCUS_OUT) {
			twin.mode.reset(WinMode::FOCUSED);
		}
	} else if ((Atom)e->xclient.data.l[0] == x11.wmdeletewin) {
		Nst::getTTY().hangup();
		exit(0);
	}
}

void resize(XEvent *e) {
	if (e->xconfigure.width == twin.w && e->xconfigure.height == twin.h)
		return;

	cresize(e->xconfigure.width, e->xconfigure.height);
}

void Nst::waitForWindowMapping() {
	xpp::Event ev;
	auto w = m_term_win.w, h = m_term_win.h;

	/* Waiting for window mapping */
	do {
		m_x11.display->getNextEvent(ev);
		/*
		 * This XFilterEvent call is required because of XOpenIM. It
		 * does filter out the key event and some client message for
		 * the input method too.
		 */
		if (ev.filterEvent())
			continue;

		if (ev.isConfigureNotify()) {
			const auto &configure = ev.toConfigureNotify();
			w = configure.width;
			h = configure.height;
		}
	} while (!ev.isMapNotify());

	cresize(w, h);
}

void Nst::applyCmdline(const Cmdline &cmd) {
	if (cmd.use_alt_screen.isSet()) {
		m_term.setAllowAltScreen(cmd.use_alt_screen.getValue());
	} else {
		m_term.setAllowAltScreen(config::ALLOWALTSCREEN);
	}

	if (cmd.fixed_geometry.isSet()) {
		m_x11.isfixed = true;
	}

	if (cmd.window_geometry.isSet()) {
		m_x11.gm = XParseGeometry(
			cmd.window_geometry.getValue().c_str(),
			&x11.l, &x11.t, &cols, &rows
		);
	}
}

Nst::Nst() :
		m_term_win(twin),
		m_x11(x11),
		m_tty(&m_term),
		m_term(m_tty, m_selection),
		m_selection(m_term) {
	if (the_instance) {
		cosmos_throw (cosmos::UsageError("more than once Nst instances alive"));
	}
	the_instance = this;
	xsetcursor(config::CURSORSHAPE);
}

void Nst::run(int argc, const char **argv) {
	cmdline.parse(argc, argv);
	cols = std::max(cols, 1U);
	rows = std::max(rows, 1U);
	m_term.init(cols, rows);
	applyCmdline(cmdline);

	setlocale(LC_CTYPE, "");
	XSetLocaleModifiers("");
	xinit();
	xsetenv();
	mainLoop();
}

void Nst::mainLoop() {
	auto ttyfd = m_tty.create(cmdline);

	auto &display = *x11.display;
	auto childfd = m_tty.getChildFD();
	auto xfd = display.getConnectionNumber();

	cosmos::Poller poller;
	poller.create();
	for (auto fd: {ttyfd, xfd, childfd}) {
		poller.addFD(fd, cosmos::Poller::MonitorMask({cosmos::Poller::MonitorSetting::INPUT}));
	}

	xpp::Event ev;
	bool drawing = false;
	cosmos::MonotonicStopWatch draw_watch, blink_watch(cosmos::MonotonicStopWatch::InitialMark(true));
	std::chrono::milliseconds timeout(-1);

	waitForWindowMapping();

	while (true) {
		if (display.hasPendingEvents())
			timeout = std::chrono::milliseconds(0);  /* existing events might not set xfd */

		auto events = poller.wait(timeout.count() >= 0 ?
				std::optional<std::chrono::milliseconds>(timeout) :
				std::nullopt);

		bool draw_event = false;

		for (const auto &event: events) {
			if (event.fd() == childfd)
				m_tty.sigChildEvent();
			else if (event.fd() == ttyfd) {
				m_tty.read();
				draw_event = true;
			}
		}

		while (display.hasPendingEvents()) {
			draw_event = true;
			display.getNextEvent(ev);
			if (ev.filterEvent())
				continue;
			else if (auto it = handlers.find(ev.getType()); it != handlers.end()) {
				auto handler = it->second;
				handler(ev.raw());
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
		if (draw_event) {
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
		if (config::BLINKTIMEOUT.count() > 0 && m_term.testAttrSet(Attr::BLINK)) {
			timeout = config::BLINKTIMEOUT - blink_watch.elapsed();
			if (timeout.count() <= 0) {
				if (-timeout.count() > config::BLINKTIMEOUT.count()) /* start visible */
					m_term_win.mode.set(WinMode::BLINK);
				m_term_win.mode.flip(WinMode::BLINK);
				m_term.setDirtyByAttr(Attr::BLINK);
				blink_watch.mark();
				timeout = config::BLINKTIMEOUT;
			}
		}

		m_term.draw();
		display.flush();
		drawing = false;
	}
}

} // end ns nst

int main(int argc, const char **argv) {
	try {
		nst::Nst nst;
		xpp::Init xpp;
		nst.run(argc, argv);
	} catch (const std::exception &ex) {
		std::cerr << ex.what() << std::endl;
		return EXIT_FAILURE;
	}

	return 0;
}
