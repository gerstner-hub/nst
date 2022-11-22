#ifndef NST_X_HXX
#define NST_X_HXX

// C++
#include <functional>

/*
 * private data types for x.cxx
 */

namespace nst {

typedef Glyph::Attr Attr;
typedef XftColor Color;
typedef std::function<void (XEvent&)> XEventCallback;

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

} // end ns

#endif
