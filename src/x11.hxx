#ifndef NST_X_HXX
#define NST_X_HXX

// libc
#include <math.h>

// C++
#include <string>

// X11
#include <X11/cursorfont.h>

// X++
#include "X++/AtomMapper.hxx"
#include "X++/types.hxx"
#include "X++/XDisplay.hxx"
#include "X++/XWindow.hxx"

// nst
#include "font.hxx"
#include "Input.hxx"
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
	void setPixmap(xpp::PixMapID pm) { m_pixmap = pm; }
	std::tuple<Font*, FontFlags> fontForMode(const Glyph::AttrBitMask &mode);
	void setForeground(const FontColor &color);
	void setForeground(size_t colidx) {
		setForeground(col[colidx]);
	}
	void fillRectangle(const DrawPos &pos, const Extent &ext);
	void sanitizeColor(Glyph &g) const;

	const FontColor& defaultFG() const { return col[config::DEFAULT_FG]; }
	const FontColor& defaultBG() const { return col[config::DEFAULT_BG]; }

protected: // data
	xpp::XDisplay *m_display = nullptr;
	xpp::PixMapID m_pixmap;
	xpp::GcSharedPtr m_gc;
};

/// This contains central X11 graphics, input and font handling
class X11 {
public: // types

	friend class XEventHandler;

protected: // data
	
	nst::Nst &m_nst;
	xpp::XWindow m_window; /// the main (and only) terminal window
	Input m_input; /// X11 input handling logic

	xpp::XDisplay *m_display = nullptr;
	const Cmdline *m_cmdline = nullptr;
	int m_screen = -1;
	Visual *m_visual = nullptr;
	int m_geometry = 0; /* geometry mask */
	bool m_fixed_geometry = false;
	DrawPos m_win_offset;
	XSetWindowAttributes m_win_attrs;
	xpp::PixMapID m_pixmap = xpp::PixMapID::INVALID;
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
	Input& input() { return m_input; }
	void setInputSpot(const CharPos pos) {
		m_input.setSpot(m_twin.toDrawPos(pos));
	}
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

	auto& display() { return *(m_display); }
	const xpp::XWindow& window() const { return m_window; }
	xpp::XWindow& window() { return m_window; }
	auto& selection() { return m_xsel; }
	auto& termWin() const { return m_twin; }
	auto& termSize() const { return m_tsize; }
	bool canDraw() const { return m_twin.checkFlag(WinMode::VISIBLE); }

protected: // functions

	auto rawDisplay() { return static_cast<Display*>(*m_display); }
	int gravity();
	void loadColors();
	int loadFont(Font *f, FcPattern *pattern);
	int loadColor(size_t i, const char *name, FontColor *ncolor);
	//! clear a rectangular font area using absolute coordinates, using the current background color
	void clearRect(const DrawPos &pos1, const DrawPos &pos2);
	//! draw a rectangular font area using a starting point and extent
	void drawRect(const FontColor &col, const DrawPos &start, const Extent &ext);
	bool loadFonts(const std::string &fontstr, double fontsize);
	void loadFontsOrThrow(const std::string&, double fontsize=0);
	void unloadFonts();
	/// udpates the specs in \c specs to display the \c len glyphs found and \c glyphs
	size_t makeGlyphFontSpecs(XftGlyphFontSpec *specs, const Glyph *glyphs, size_t len, const CharPos &loc);
	/// looks up the matching XftFont and Glyph index for the given rune and Font
	std::tuple<XftFont*, FT_UInt> lookupFontEntry(const Rune rune, Font &fnt, const FontFlags flags);
	void glyphColors(const Glyph base, FontColor &fg, FontColor &bg);
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
	xpp::XWindow parent() const;
	const FontColor& cursorColor(const CharPos &pos, Glyph &glyph) const;
};

} // end ns

#endif
