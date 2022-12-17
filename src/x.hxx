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
#include "XSelection.hxx"

/*
 * private data types for x.cxx
 */

namespace nst {

typedef Glyph::Attr Attr;

class Color : public XftColor {
public:
	void invert() {
		color.red = ~color.red;
		color.green = ~color.green;
		color.blue = ~color.blue;
	}

	Color inverted() const {
		auto ret = Color(*this);
		ret.invert();
		return ret;
	}

	void makeFaint() {
		color.red /= 2;
		color.green /= 2;
		color.blue /= 2;
	}

	Color faint() const {
		auto ret = Color(*this);
		ret.makeFaint();
		return ret;
	}

	void assignTo(XRenderColor &xc) const {
		xc.red = color.red;
		xc.green = color.green;
		xc.blue = color.blue;
		xc.alpha = color.alpha;
	}
};

/* Drawing Context */
struct DrawingContext {
	std::vector<Color> col;
	Font font, bfont, ifont, ibfont;
	GC gc;
};

struct X11 {
public: // types

	struct Input {
	protected: // data
		XIM m_method = nullptr;
		XIC m_ctx = nullptr;
		XPoint m_spot = {0, 0};
		XVaNestedList m_spotlist = nullptr;
		X11 &m_x11;
	protected: // functions
		static void destroyMethodCB(XIM, XPointer, XPointer);
		static int destroyContextCB(XIC, XPointer, XPointer);
		static void instMethodCB(Display*, XPointer, XPointer);
		void destroyMethod();
		int destroyContext();
		void instMethod();
	public:
		Input(X11 &x) : m_x11(x) {}
		bool open();
		void setSpot(const CharPos &chp);
		void installCallback();
		bool haveContext() const { return m_ctx != nullptr; }
		XIC getContext() { return m_ctx; }
		void setFocus();
		void unsetFocus();
	};

protected: // data
	
	Input m_input;

	xpp::XDisplay *m_display = nullptr;
	const Cmdline *m_cmdline = nullptr;
	int m_screen = -1;
	Visual *m_visual = nullptr;
	xpp::XWindow m_window; // the main (and only) terminal window
	xpp::XAtomMapper *m_mapper = nullptr;
	int m_geometry = 0; /* geometry mask */
	bool m_fixed_geometry = false;
	int m_left_offset = 0;
	int m_top_offset = 0;
	XSetWindowAttributes m_win_attrs;
	Drawable m_draw_buf;
	Atom m_netwmname;
	Atom m_netwmiconname;
	Atom m_wmdeletewin;
	DrawingContext m_draw_ctx;
	bool m_colors_loaded = false;
	Colormap m_color_map;

	std::vector<Fontcache> m_font_cache;
	/* Fontcache is an array now. A new font will be appended to the array. */
	double m_used_font_size = 0;
	double m_default_font_size = 0;
	std::vector<XftGlyphFontSpec> m_font_specs; /* font spec buffer used for rendering */
	XftDraw *m_font_draw = nullptr;
	XSelection m_xsel;

public: // functions

	X11() : m_input(*this), m_xsel(*this) {}
	auto getRawDisplay() { return static_cast<Display*>(*m_display); }
	auto& getDisplay() { return *(m_display); }
	Atom getAtom(const char *name) const { return m_mapper->getAtom(name); }
	void pasteSelection();
	void pasteClipboard();
	void copyToClipboard();
	void zoomFont(float val);
	void resetFont();
	void toggleNumlock();
	void setUrgency(int add);
	void resize(const TermSize &dim);
	void setHints();
	/// xim (X input method) setup
	Input& getInput() { return m_input; }
	void init();
	void setGeometry(const std::string &g);
	void setPointerMotion(bool on_off);
	void finishDraw();
	void changeEventMask(long event, bool on_off);
	void setIconTitle(const std::string &title);
	void setTitle(const std::string &title);
	void loadColors();
	bool getColor(size_t idx, unsigned char *r, unsigned char *g, unsigned char *b) const;
	bool setColorName(size_t idx, const char *name);
	void drawLine(const Line &line, int x1, int y1, int x2);
	void drawCursor(int cx, int cy, Glyph g, int ox, int oy, Glyph og);
	void setFixedGeometry(bool fixed) {
		m_fixed_geometry = fixed;
	}
	const xpp::XWindow& getWindow() const { return m_window; }
	const Atom& getWmDeleteWin() const { return m_wmdeletewin; }
	auto& getXSelection() { return m_xsel; }
protected:
	int getGravity();
	int loadFont(Font *f, FcPattern *pattern);
	void unloadFont(Font *f);
	std::tuple<Font*, FRC> getFontForMode(const Glyph::AttrBitMask &mode);
	int loadColor(size_t i, const char *name, Color *ncolor);
	void clearRect(const DrawPos &pos1, const DrawPos &pos2);
	bool loadFonts(const std::string &fontstr, double fontsize);
	void loadFontsOrThrow(const std::string&, double fontsize=0);
	void unloadFonts();
	bool ximOpen();
	size_t makeGlyphFontSpecs(XftGlyphFontSpec *specs, const Glyph *glyphs, size_t len, int x, int y);
	void drawGlyphFontSpecs(const XftGlyphFontSpec *specs, Glyph base, size_t len, int x, int y);
	void drawGlyph(Glyph g, int x, int y);
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
