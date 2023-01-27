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

/// X11 drawing specific data and logic
struct DrawingContext {
	std::vector<FontColor> col;
	Font font, bfont, ifont, ibfont;

public: // functions
	void createGC(xpp::XDisplay &display, xpp::XWindow &parent);
	void freeGC() { m_gc.reset(); }
	void setRawGC(xpp::GcSharedPtr gc) { m_gc = gc; }
	auto getRawGC() { return m_gc.get(); }
	auto getGC() { return m_gc; }
	void setPixmap(xpp::PixMap &pm) { m_pixmap = pm; }
	std::tuple<Font*, FontFlags> getFontForMode(const Glyph::AttrBitMask &mode);
	void setForeground(const FontColor &color);
	void setForeground(size_t colidx) {
		setForeground(col[colidx]);
	}
	void fillRectangle(const DrawPos &pos, const Extent &ext);
	void sanitizeColor(Glyph &g) const;

	const FontColor& getDefaultFG() const { return col[config::DEFAULTFG]; }
	const FontColor& getDefaultBG() const { return col[config::DEFAULTBG]; }

protected: // data
	xpp::XDisplay *m_display = nullptr;
	xpp::PixMap m_pixmap;
	xpp::GcSharedPtr m_gc;
};

/// Wrapper around the XRenderColor primitive that adds some helper functions
struct RenderColor : public XRenderColor {
	RenderColor() {}
	explicit RenderColor(const Glyph::color_t rgb) {
		setFromRGB(rgb);
	}
	explicit RenderColor(const FontColor &c) {
		c.assignTo(*this);
	}
	void setFromRGB(const Glyph::color_t rgb);
};

/// This contains central X11 graphics, input and font handling
struct X11 {
public: // types

	friend class XEventHandler;

	/// X11 Input Method handling
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
		~Input();
		bool open();
		void setSpot(const CharPos &chp);
		void installCallback();
		bool haveContext() const { return m_ctx != nullptr; }
		XIC getContext() { return m_ctx; }
		void setFocus();
		void unsetFocus();
		/// looks up a KeySym and string representation of the given event
		KeySym lookupString(const XKeyEvent &ev, std::string &s);
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
	DrawPos m_win_offset;
	XSetWindowAttributes m_win_attrs;
	xpp::PixMap m_pixmap;
	xpp::XAtom m_netwmname;
	xpp::XAtom m_wmname;
	xpp::XAtom m_netwmiconname;
	xpp::XAtom m_wmdeletewin;
	DrawingContext m_draw_ctx;
	bool m_colors_loaded = false;
	bool m_fc_inited = false;
	Colormap m_color_map = xpp::INVALID_XID;

	std::vector<FontCache> m_font_cache;
	double m_used_font_size = 0;
	double m_default_font_size = 0;
	std::vector<XftGlyphFontSpec> m_font_specs; /* font spec buffer used for rendering */
	XftDraw *m_font_draw = nullptr;
	XSelection m_xsel;

	TermSize m_tsize;
	TermWindow m_twin;

public: // functions

	explicit X11(Nst &nst);
	~X11();

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
	void setupCursor();
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
	void drawLine(const Line &line, const CharPos &start, const int count);
	void clearCursor(const CharPos &pos, Glyph glyph);
	void drawCursor(const CharPos &pos, Glyph glyph);
	void setMode(const WinMode &flag, const bool set);
	void setCursorStyle(const CursorStyle &cursor);
	void ringBell();

	void setWinSize(const Extent &ext) {
		m_twin.setWinExtent(ext);
	}
	void setBlinking(const bool blinking) {
		if (blinking)
			m_twin.setFlag(WinMode::BLINK);
		else
			m_twin.resetFlag(WinMode::BLINK);
	}
	void switchBlinking() {
		m_twin.flipFlag(WinMode::BLINK);
	}

	auto& getDisplay() { return *(m_display); }
	Atom getAtom(const char *name) const { return getXAtom(name).get(); }
	xpp::XAtom getXAtom(const char *name) const { return m_mapper->getAtom(name); }
	const xpp::XWindow& getWindow() const { return m_window; }
	xpp::XWindow& getWindow() { return m_window; }
	auto& getWmDeleteWin() const { return m_wmdeletewin; }
	auto& getXSelection() { return m_xsel; }
	auto& getTermWin() const { return m_twin; }
	auto& getTermSize() const { return m_tsize; }
	bool canDraw() const { return m_twin.checkFlag(WinMode::VISIBLE); }

protected: // functions

	auto getRawDisplay() { return static_cast<Display*>(*m_display); }
	int getGravity();
	void loadColors();
	int loadFont(Font *f, FcPattern *pattern);
	void unloadFont(Font *f);
	int loadColor(size_t i, const char *name, FontColor *ncolor);
	//! clear a rectangular font area using absolute coordinates, using the current background color
	void clearRect(const DrawPos &pos1, const DrawPos &pos2);
	//! draw a rectangular font area using a starting point and extent
	void drawRect(const FontColor &col, const DrawPos &start, const Extent &ext);
	bool loadFonts(const std::string &fontstr, double fontsize);
	void loadFontsOrThrow(const std::string&, double fontsize=0);
	void unloadFonts();
	bool ximOpen();
	/// udpates the specs in \c specs to display the \c len glyphs found and \c glyphs
	size_t makeGlyphFontSpecs(XftGlyphFontSpec *specs, const Glyph *glyphs, size_t len, const CharPos &loc);
	/// looks up the matching XftFont and Glyph index for the given rune and Font
	std::tuple<XftFont*, FT_UInt> lookupFontEntry(const Rune rune, Font &fnt, const FontFlags flags);
	void getGlyphColors(const Glyph base, FontColor &fg, FontColor &bg);
	void drawGlyphFontSpecs(const XftGlyphFontSpec *specs, Glyph base, size_t len, const CharPos &loc);
	void drawGlyph(Glyph g, const CharPos &loc);
	void embeddedFocusChange(const bool in_focus);
	void focusChange(const bool in_focus);
	void setVisible(const bool visible) {
		m_twin.setFlag(WinMode::VISIBLE, visible);
	}
	/// (re)allocate the m_pixmap buffer and related context according to the current window size
	void allocPixmap();
	/// returns the parent window to be used as parent of the terminal window
	xpp::XWindow getParent() const;
	const FontColor& getCursorColor(const CharPos &pos, Glyph &glyph) const;
};

} // end ns

#endif
