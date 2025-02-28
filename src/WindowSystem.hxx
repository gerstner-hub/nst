#pragma once

// C++
#include <deque>
#include <string_view>

// cosmos
#include "cosmos/SysString.hxx"

// xpp
#include "xpp/fwd.hxx"
#include "xpp/GraphicsContext.hxx"
#include "xpp/Pixmap.hxx"
#include "xpp/SetWindowAttributes.hxx"
#include "xpp/types.hxx"
#include "xpp/utils.hxx"
#include "xpp/XCursor.hxx"
#include "xpp/XWindow.hxx"

// nst
#include "color.hxx"
#include "font.hxx"
#include "Input.hxx"
#include "TermWindow.hxx"
#include "XSelection.hxx"

namespace nst {

/// This is the central interface towards X11 graphics, input and font handling.
class WindowSystem {
	friend class XEventHandler;
public: // functions

	explicit WindowSystem(Nst &nst);
	~WindowSystem();

	/// Basic initial setup of all necessary structures like the XWindow to use.
	void init();

	/// Request the current primary selection for pasting on the terminal.
	void pasteSelection();
	/// Request the current clipboard selection for pasting on the terminal
	void pasteClipboard();
	/// Copy the primary selection into the clipboard
	void copyToClipboard();

	/// Zoom the terminal fonts in/out by the given value in pixels.
	void zoomFont(double val);
	/// Reset terminal fonts to the default size.
	void resetFont();

	/// (Re-)Adjust graphic structure to the given terminal dimensions.
	void resize(const TermSize dim);
	/// Apply a new graphical window size.
	void setWinSize(const Extent ext) {
		m_twin.setWinExtent(ext);
	}

	/// Report the current input (cursor) location to XInput.
	void setInputSpot(const CharPos pos) {
		// it seems the input spot should be set at the bottom of the
		// cursor, so jump to the next line coordinate (see original
		// xximpot() in ST).
		m_input.setSpot(m_twin.toDrawPos(pos.nextLine()));
	}

	/// reset colors and titles to the initial state
	void resetState() {
		setDefaultTitle();
		m_color_manager.resetColors();
		m_twin.reset();
	}

	void resetColors() {
		m_color_manager.resetColors();
	}

	void setPointerMotion(bool on_off) {
		if (!on_off && !m_is_pointer_visible)
			return;
		changeEventMask(xpp::EventMask::POINTER_MOTION, on_off);
	}
	/// To be called when a set of drawing operation is finished and new data should be displayed.
	void finishDraw();
	/// Returns whether drawing is currently possible (or sensible).
	bool canDraw() const { return m_twin.checkFlag(WinMode::VISIBLE); }

	void setIconTitle(const std::string_view title);
	void setDefaultIconTitle();
	void setTitle(const std::string_view title);
	void setDefaultTitle();

	void toggleFullScreen();

	/// Push the current window title on the title stack for later restore via popTitle().
	void pushTitle();
	/// Restore the last window title stored via pushTitle().
	void popTitle();

	void clearCursor(const CharPos pos, Glyph glyph);
	void drawCursor(const CharPos pos, Glyph glyph);
	void setCursorStyle(const CursorStyle cursor);

	/// Draw a range of Glyphs from it to end starting at coordinate start_pos
	void drawGlyphs(Line::const_iterator it, const Line::const_iterator end, CharPos start_pos);

	/// Change the given WinMode setting.
	/**
	 * This is used by escape handling parsers to trigger requested.
	 * actions.
	 **/
	void setMode(const WinMode flag, const bool set);

	/// Ring the XKeyboard bell.
	void ringBell();
	/// Toggle keyboard numlock state.
	void toggleNumlock();
	/// Update state after the global theme has been changed.
	void themeChanged();

	/// Set the terminal wide blinking state.
	void setBlinking(const bool blinking) {
		if (blinking)
			m_twin.setFlag(WinMode::BLINK);
		else
			m_twin.resetFlag(WinMode::BLINK);
	}
	/// Flip the terminal wide blinking state.
	void switchBlinking() {
		m_twin.flipFlag(WinMode::BLINK);
	}

	bool isBlinkingCursorStyle() const {
		return m_blinking_cursor_style;
	}

	void hidePointer();
	void showPointer();

	void invertColors();

	void clearWindow();

	const xpp::XWindow& window() const { return m_window; }
	xpp::XWindow& window() { return m_window; }
	auto& selection() { return m_selection; }
	auto& termWin() const { return m_twin; }
	auto& colorManager() { return m_color_manager; }

protected: // functions

	/// Apply command line and configuration file settings.
	void applyConfig(TermSize &tsize);

	/// Change whether the given event types will be reported by X11.
	void changeEventMask(const xpp::EventMask event, bool on_off);
	void setupPointer();
	void setupWindow(const xpp::XWindow &parent);
	void setupWinAttrs();
	void setSizeHints();
	/// Parses the given X window geometry string and adjusts internal data structures accordingly.
	void setGeometry(const cosmos::SysString str, TermSize &tsize);
	xpp::Gravity gravity() const;
	/// Clear a rectangular font area using absolute coordinates, using the current background color.
	void clearRect(const DrawPos pos1, const DrawPos pos2);
	void unloadFonts();

	/// Loads specs into `m_font_specs` to display the `count` glyphs found and `glyphs`.
	/**
	 * \param[in] char_pos Start position on the terminal where to start drawing the glyphs.
	 **/
	void makeGlyphFontSpecs(const Glyph *glyphs, const size_t count, const CharPos char_pos);

	/// Draw Glyphs for which GlyphFontSpecs have been prepared previously via makeGlyphFontSpecs()
	/**
	 * \param[in] count The number of Glyphs to draw.
	 * \param[in] base The template Glyph properties that all `count`
	 * following glyphs will share.
	 **/
	void drawGlyphFontSpecs(Glyph base, const size_t count, const CharPos char_pos);

	/// Draw the given single Glyph at position `loc`.
	void drawGlyph(const Glyph g, const CharPos loc);

	// Intelligent cleaning up of the borders.
	void cleanupWindowBorders(const int textwidth, const CharPos char_pos, const DrawPos draw_pos);

	void embeddedFocusChange(const bool in_focus);
	void focusChange(const bool in_focus);

	void setVisible(const bool visible) {
		m_twin.setFlag(WinMode::VISIBLE, visible);
	}

	void setUrgency(const bool have_urgency);

	/// (re)allocate the m_pixmap buffer and related context according to the current window size.
	void allocPixmap();
	/// Returns the window to be used as parent of the terminal window.
	xpp::XWindow parent() const;
	void createGraphicsContext(const xpp::XWindow &parent);

	void handleFontUpdate();

protected: // data

	Nst &m_nst;
	const Cmdline &m_cmdline;
	xpp::XWindow m_window; ///< the main (and only) terminal window
	Input m_input; ///< X11 input handling logic
	TermWindow m_twin;
	FontManager m_font_manager;
	FontDrawContext m_font_draw_ctx;
	ColorManager m_color_manager;
	XSelection m_selection;
	bool m_blinking_cursor_style = false;
	int m_border_pixels = 0;
	int m_cursor_thickness = 1;
	bool m_initialized = false;

	xpp::XDisplay &m_display;
	xpp::GeometrySettingsMask m_geometry_mask;
	xpp::WindowSpec m_win_geometry;
	xpp::SetWindowAttributes m_win_attrs;
	xpp::Pixmap m_pixmap;
	xpp::GraphicsContext m_graphics_context;
	xpp::XCursor m_font_pointer;
	xpp::XCursor m_blank_pointer;
	bool m_hide_mouse_cursor = true;
	bool m_is_pointer_visible = true;

	GlyphFontSpecVector m_font_specs;
	/// To keep track of the remaining font specs to draw in drawGlyphFontSpecs()
	GlyphFontSpecVector::iterator m_next_font_spec;

	static constexpr size_t MAX_TITLE_STACK_SIZE = 10;
	std::deque<std::string> m_title_stack;
};

} // end ns
