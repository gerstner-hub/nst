#ifndef NST_X_HXX
#define NST_X_HXX

// libc
#include <math.h>
// stdlib
#include <string>
// libX11
#include <X11/Xft/Xft.h>
#include <X11/cursorfont.h>
// X++
#include "X++/XDisplay.hxx"
#include "X++/XAtom.hxx"
#include "X++/XWindow.hxx"
// nst
#include "font.hxx"
#include "nst_config.hxx"
#include "TermWindow.hxx"
#include "XSelection.hxx"

namespace nst {

/* Drawing Context */
struct DrawingContext {
	std::vector<Color> col;
	Font font, bfont, ifont, ibfont;
	GC gc;
};

struct X11 {
public: // types

	friend class XEventHandler;

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
	
	nst::Nst &m_nst;
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
	xpp::PixMap m_pixmap;
	Atom m_netwmname;
	Atom m_netwmiconname;
	Atom m_wmdeletewin;
	DrawingContext m_draw_ctx;
	bool m_colors_loaded = false;
	Colormap m_color_map = xpp::INVALID_XID;

	std::vector<Fontcache> m_font_cache;
	/* Fontcache is an array now. A new font will be appended to the array. */
	double m_used_font_size = 0;
	double m_default_font_size = 0;
	std::vector<XftGlyphFontSpec> m_font_specs; /* font spec buffer used for rendering */
	XftDraw *m_font_draw = nullptr;
	XSelection m_xsel;

	TermSize m_tsize;
	TermWindow m_twin;

public: // functions

	explicit X11(Nst &nst);

	void pasteSelection();
	void pasteClipboard();
	void copyToClipboard();
	void zoomFont(float val);
	void resetFont();
	void toggleNumlock();
	void setUrgency(int add);
	void resize(const TermSize &dim);
	void setHints();
	Input& getInput() { return m_input; }
	void init();
	/// reset the initial state
	void resetState() {
		setDefaultTitle();
		loadColors();
	}
	void setGeometry(const std::string &g);
	void setPointerMotion(bool on_off);
	void finishDraw();
	void changeEventMask(long event, bool on_off);
	void setIconTitle(const std::string &title);
	void setDefaultIconTitle();
	void setTitle(const std::string &title);
	void setDefaultTitle();
	bool getColor(size_t idx, unsigned char *r, unsigned char *g, unsigned char *b) const;
	bool setColorName(size_t idx, const char *name);
	void drawLine(const Line &line, int x1, int y1, int x2);
	void drawCursor(int cx, int cy, Glyph g, int ox, int oy, Glyph og);
	void setMode(const WinMode &flag, const bool set);
	void setCursorStyle(const CursorStyle &cursor);
	void ringBell();

	void setWinSize(const Extent &ext) {
		m_twin.setWinExtent(ext);
	}
	void setBlinking(const bool blinking) {
		if (blinking)
			m_twin.mode.set(WinMode::BLINK);
		else
			m_twin.mode.reset(WinMode::BLINK);
	}
	void switchBlinking() {
		m_twin.mode.flip(WinMode::BLINK);
	}

	auto& getDisplay() { return *(m_display); }
	Atom getAtom(const char *name) const { return getXAtom(name).get(); }
	xpp::XAtom getXAtom(const char *name) const { return m_mapper->getAtom(name); }
	const xpp::XWindow& getWindow() const { return m_window; }
	const Atom& getWmDeleteWin() const { return m_wmdeletewin; }
	auto& getXSelection() { return m_xsel; }
	auto& getTermWin() const { return m_twin; }
	auto& getTermSize() const { return m_tsize; }
	bool canDraw() const { return m_twin.mode[WinMode::VISIBLE]; }

protected: // functions

	auto getRawDisplay() { return static_cast<Display*>(*m_display); }
	int getGravity();
	void loadColors();
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
	void embeddedFocusChange(const bool in_focus);
	void focusChange(const bool in_focus);
	void setVisible(const bool visible) {
		m_twin.mode.set(WinMode::VISIBLE, visible);
	}
	/// (re)allocate the m_pixmap buffer and related context according to the current window size
	void allocPixmap();
	static const char* getColorName(size_t nr);
};

} // end ns

#endif
