#ifndef NST_X_HXX
#define NST_X_HXX

/*
 * private data types for x.cxx
 */

namespace nst {

typedef Glyph::Attr Attr;
typedef XftColor Color;

struct X11 {
	xpp::XDisplay *display = nullptr;
	xpp::XAtomMapper *mapper = nullptr;
	Colormap cmap;
	xpp::XWindow win;
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

/* Drawing Context */
struct DrawingContext {
	std::vector<Color> col;
	Font font, bfont, ifont, ibfont;
	GC gc;
};

/* Purely graphic info */
struct TermWindow {
	Extent tty; /* tty extent (window minus border size) */
	Extent win; /* window width and height */
	Extent chr; /* single character dimensions */
	WinModeMask mode; /* window state/mode flags */
	CursorStyle cursor;

	void setCharSize(const DrawingContext &dc) {
		chr.width = ceilf(dc.font.width * config::CWSCALE);
		chr.height = ceilf(dc.font.height * config::CHSCALE);
	}

	void setWinExtent(const Extent &ext) {
		if (ext.width != 0)
			win.width = ext.width;
		if (ext.height != 0)
			win.height = ext.height;
	}

	void setWinExtent(const TermSize &size) {
		win.width  = 2 * config::BORDERPX + size.cols * chr.width;
		win.height = 2 * config::BORDERPX + size.rows * chr.height;
	}

	//! calculates the number of characters that fit into the current
	//! terminal window
	TermSize getTermDim() const {
		auto EXTRA_PIXELS = 2 * config::BORDERPX;
		int cols = (win.width - EXTRA_PIXELS) / chr.width;
		int rows = (win.height - EXTRA_PIXELS) / chr.height;
		cols = std::max(1, cols);
		rows = std::max(1, rows);
		return TermSize{cols, rows};
	}

	void setTermDim(const TermSize &chars) {
		tty.width = chars.cols * chr.width;
		tty.height = chars.rows * chr.height;
	}

	DrawPos getDrawPos(const CharPos &cp) const {
		DrawPos dp;
		dp.x = config::BORDERPX + cp.x * chr.width;
		dp.y = config::BORDERPX + cp.y * chr.height;
		return dp;
	}

	DrawPos getNextCol(const DrawPos &pos) const {
		return DrawPos{pos.x + chr.width, pos.y};
	}

	DrawPos getNextLine(const DrawPos &pos) const {
		return DrawPos{pos.x, pos.y + chr.height};
	}

	CharPos getCharPos(const DrawPos &pos) const {
		CharPos ret{pos.x - config::BORDERPX, pos.y - config::BORDERPX};

		ret.clampX(tty.width - 1);
		ret.x /= chr.width;

		ret.clampY(tty.height - 1);
		ret.y /= chr.height;

		return ret;
	}
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
static void cresize(const Extent & = {0,0});
static void xresize(const TermSize &chars);
static void xhints(void);
static int xloadcolor(size_t, const char *, Color *);
static int xloadfont(Font *, FcPattern *);
static void xunloadfont(Font *);
static void xunloadfonts(void);
static void xsetenv(void);
static void xseturgency(int);

static void setsel(const char*, Time);
static bool match(uint, uint);
static void xloadfontsOrThrow(const std::string&, double fontsize=0);

} // end ns

#endif
