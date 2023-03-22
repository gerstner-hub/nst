#ifndef NST_X_HXX
#define NST_X_HXX

// C++
#include <string_view>
#include <vector>

// X++
#include "X++/GraphicsContext.hxx"
#include "X++/Pixmap.hxx"
#include "X++/types.hxx"
#include "X++/utils.hxx"
#include "X++/XDisplay.hxx"
#include "X++/XWindow.hxx"

// nst
#include "color.hxx"
#include "font.hxx"
#include "Input.hxx"
#include "nst_config.hxx"
#include "TermWindow.hxx"
#include "XSelection.hxx"

namespace nst {

/// This contains central X11 graphics, input and font handling
class X11 {
	friend class XEventHandler;
public: // functions

	explicit X11(Nst &nst);
	~X11();

	void pasteSelection();
	void pasteClipboard();
	void copyToClipboard();
	void zoomFont(double val);
	void resetFont();
	void toggleNumlock();
	void resize(const TermSize dim);
	void setInputSpot(const CharPos pos) {
		m_input.setSpot(m_twin.toDrawPos(pos));
	}
	void init();
	/// reset to the initial state
	void resetState() {
		setDefaultTitle();
		m_color_manager.resetColors();
	}
	void setPointerMotion(bool on_off) {
		changeEventMask(xpp::EventMask::PointerMotion, on_off);
	}
	void finishDraw();
	void setIconTitle(const std::string_view title);
	void setDefaultIconTitle();
	void setTitle(const std::string_view title);
	void setDefaultTitle();
	void clearCursor(const CharPos pos, Glyph glyph);
	void drawCursor(const CharPos pos, Glyph glyph);
	void drawGlyphs(Line::const_iterator it, const Line::const_iterator end, CharPos start_pos);
	void setMode(const WinMode flag, const bool set);
	void setCursorStyle(const CursorStyle cursor);
	void ringBell();

	void setWinSize(const Extent ext) {
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

	const xpp::XWindow& window() const { return m_window; }
	xpp::XWindow& window() { return m_window; }
	auto& selection() { return m_xsel; }
	auto& termWin() const { return m_twin; }
	auto& colorManager() { return m_color_manager; }
	bool canDraw() const { return m_twin.checkFlag(WinMode::VISIBLE); }

protected: // functions

	void changeEventMask(const xpp::EventMask event, bool on_off);
	void setupCursor();
	void setupWindow(xpp::XWindow &parent);
	void setupWinAttrs();
	void setSizeHints();
	void setGeometry(const std::string_view str, TermSize &tsize);
	xpp::Gravity gravity() const;
	//! clear a rectangular font area using absolute coordinates, using the current background color
	void clearRect(const DrawPos pos1, const DrawPos pos2);
	void clearWindow();
	void unloadFonts();
	/// loads specs into \c m_font_specs to display the \c len glyphs found and \c glyphs
	void makeGlyphFontSpecs(const Glyph *glyphs, const size_t count, const CharPos ch_pos);
	void drawGlyphFontSpecs(Glyph base, const size_t count, const CharPos loc);
	void drawGlyph(Glyph g, const CharPos loc);
	void cleanupWindowBorders(int textwidth, const CharPos ch_pos, const DrawPos draw_pos);
	void embeddedFocusChange(const bool in_focus);
	void focusChange(const bool in_focus);
	void setVisible(const bool visible) {
		m_twin.setFlag(WinMode::VISIBLE, visible);
	}
	void setUrgency(const bool have_urgency);
	/// (re)allocate the m_pixmap buffer and related context according to the current window size
	void allocPixmap();
	/// returns the parent window to be used as parent of the terminal window
	xpp::XWindow parent() const;
	void createGraphicsContext(xpp::XWindow &parent);

protected: // data

	nst::Nst &m_nst;
	const Cmdline &m_cmdline;
	xpp::XWindow m_window; /// the main (and only) terminal window
	Input m_input; /// X11 input handling logic
	TermWindow m_twin;
	FontManager m_font_manager;
	FontDrawContext m_font_draw_ctx;
	ColorManager m_color_manager;
	XSelection m_xsel;

	xpp::XDisplay &m_display;
	xpp::GeometrySettingsMask m_geometry_mask;
	xpp::WindowSpec m_win_geometry;
	xpp::SetWindowAttributes m_win_attrs;
	xpp::Pixmap m_pixmap;
	xpp::GraphicsContext m_graphics_context;

	using GlyphFontSpecVector = std::vector<XftGlyphFontSpec>;
	GlyphFontSpecVector m_font_specs;
	GlyphFontSpecVector::iterator m_next_font_spec;
};

} // end ns

#endif
