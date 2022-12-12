#ifndef NST_X_HXX
#define NST_X_HXX

// libc
#include <math.h>
// libX11
#include <X11/Xft/Xft.h>
#include <X11/cursorfont.h>
// X++
#include "X++/XDisplay.hxx"
#include "X++/XAtom.hxx"
#include "X++/XWindow.hxx"
// nst
#include "font.hxx"
#include "nst_config.h"
#include "win.h"

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
public: // functions
	Display* getDisplay() {
		return static_cast<Display*>(*this->display);
	}
	Atom getAtom(const char *name) const {
		return mapper->getAtom(name);
	}
	void pasteSelection();
	void pasteClipboard();
	void copyToClipboard();
	void zoomFont(float val);
	void resetFont();
	void toggleNumlock();
	void setUrgency(int add);
	void resize(const TermSize &dim);
	int loadColor(size_t i, const char *name, Color *ncolor);
	void clearRect(const DrawPos &pos1, const DrawPos &pos2);
	void setHints();
	bool loadFonts(const std::string &fontstr, double fontsize);
	void loadFontsOrThrow(const std::string&, double fontsize=0);
	void unloadFonts();
protected:
	static int geomMaskToGravity(int mask);
	int loadFont(Font *f, FcPattern *pattern);
	void unloadFont(Font *f);
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

} // end ns

#endif
