#ifndef NST_X_HXX
#define NST_X_HXX

/*
 * private data types for x.cxx
 */

namespace nst {

typedef Glyph::Attr Attr;
typedef XftColor Color;

/* Purely graphic info */
struct TermWindow {
	int tw, th; /* tty width and height */
	int w, h; /* window width and height */
	int ch; /* char height */
	int cw; /* char width  */
	WinModeMask mode; /* window state/mode flags */
	CursorStyle cursor;
};

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

static void setsel(const char*, Time);
static bool match(uint, uint);
static void xloadfontsOrThrow(const std::string&, double fontsize=0);

} // end ns

#endif
