#ifndef NST_TERM_HXX
#define NST_TERM_HXX

// C++
#include <array>
#include <optional>
#include <vector>

// cosmos
#include "cosmos/BitMask.hxx"
#include "cosmos/types.hxx"

// nst
#include "CursorState.hxx"
#include "EscapeHandler.hxx"
#include "fwd.hxx"
#include "Glyph.hxx"
#include "Screen.hxx"
#include "types.hxx"

namespace nst {

/// Internal representation of the screen.
/**
 * This class maintains the terminal contents on a logical level without
 * containing the actual X11 drawing logic.
 *
 * It manages the dimensions of the terminal, each line and its contents,
 * processes input data and forwards escape sequences to EscapeHandler.
 **/
class Term {
public: // types

	/// Gobal terminal mode settings.
	enum class Mode {
		WRAP        = 1 << 0, /// automatically wrap to next line if cursor reaches end of line
		INSERT      = 1 << 1, /// if set, on input, shift existing characters in a line to the right
		ALTSCREEN   = 1 << 2, /// if set then the alternative screen is shown
		CRLF        = 1 << 3, /// implicit carriage return on newline
		TECHO       = 1 << 4, /// whether input data should be echoed on the terminal screen (ECHO identifier conflicts with X headers)
		PRINT       = 1 << 5, /// duplicate all input into I/O file
		UTF8        = 1 << 6, /// UTF8 input support
	};

	using ModeBitMask = cosmos::BitMask<Mode>;

	/// Different terminal character sets (we don't support them all).
	enum class Charset {
		GRAPHIC0, /// DEC Special Graphics 7-bit character set
		GRAPHIC1,
		UK,
		USA, // US-ASCII
		MULTI,
		GER,
		FIN
	};

	using CarriageReturn = cosmos::NamedBool<struct carriage_t, true>;
	using ShowCtrlChars = cosmos::NamedBool<struct show_ctrl_t, true>;

public: // functions

	explicit Term(Nst &nst);

	void init(const Nst &nst);

	/// Change the terminal dimensions.
	/**
	 * This performs rather complex adjustments to refit the scrolling
	 * reagion, adjust the cursor position and maintainer internal
	 * bookkeeping.
	 **/
	void resize(const TermSize new_size);

	/// Clear all terminal content and restore sane defaults.
	void reset();

	/// Clears the given character range completely.
	void clearRegion(Range range);

	/// Clears the given span of lines completely.
	void clearLines(const LineSpan span);

	/// Clears the complete screen.
	void clearScreen() {
		clearRegion({topLeft(), bottomRight()});
	}

	/// Clears all rows and cols after the current cursor position.
	/**
	 * Columns on the current line before the current cursor column will
	 * remain.
	 **/
	void clearLinesBelowCursor();

	/// Clears all rows and cols up to the current cursor position.
	/**
	 * Colums on the current line after the current cursor column will
	 * remain
	 **/
	void clearLinesAboveCursor();

	/// Clears the current line up to and including the current cursor position.
	void clearColsBeforeCursor();

	/// Clears the current line from and including the current cursor position.
	void clearColsAfterCursor();

	/// Clears the line the cursor is in completely.
	void clearCursorLine();

	/// Marks the given span of lines as dirty for redrawing.
	void setDirty(LineSpan span);

	/// Set all screen lines as dirty for redrawing.
	void setAllDirty() {
		setDirty(LineSpan{0, m_size.rows - 1});
	}

	/// Clears all currently defined tabstop positions.
	void clearAllTabs() {
		m_tabs.clear();
		m_tabs.resize(m_size.cols);
	}

	/// Select one of the four configurable character sets by index.
	void setCharset(const size_t charset) {
		m_active_charset = std::clamp(charset, 0UL, m_charsets.size() - 1);
	}

	/// Sets the given charset index to the given charset selection.
	void setCharsetMapping(const size_t index, const Charset cs) {
		if(index >= m_charsets.size())
			return;

		m_charsets[index] = cs;
	}

	/// Sets a span of lines on the screen that will be affected by scrolling operations.
	void setScrollArea(const LineSpan span);

	/// Move the cursor to the given screen position.
	void moveCursorTo(CharPos pos);

	void moveCursorUp(const size_t count, const CarriageReturn &cr = CarriageReturn(false)) {
		auto curpos = cursor().pos;
		if (cr)
			curpos.moveToStartOfLine();
		moveCursorTo(curpos.prevLine(count));
	}

	void moveCursorDown(const size_t count, const CarriageReturn &cr = CarriageReturn(false)) {
		auto curpos = cursor().pos;
		if (cr)
			curpos.moveToStartOfLine();
		moveCursorTo(curpos.nextLine(count));
	}

	void moveCursorLeft(const size_t count) {
		const auto &curpos = cursor().pos;
		moveCursorTo(curpos.prevCol(count));
	}

	void moveCursorRight(const size_t count) {
		const auto &curpos = cursor().pos;
		moveCursorTo(curpos.nextCol(count));
	}

	void moveCursorToCol(const int col) {
		const auto &curpos = cursor().pos;
		moveCursorTo(CharPos{col, curpos.y});
	}

	/// Move cursor ignoring scroll limit (if ORIGIN CursorState is active).
	void moveCursorAbsTo(CharPos pos);

	/// Switch to or away from alt screen.
	/**
	 * If alt screen handling is disabled then this function does nothing.
	 *
	 * \param[in] with_cursor If set then also the cursor position will be
	 * saved (on enable) or restore (on disable).
	 **/
	void setAltScreen(const bool enable, const bool with_cursor);

	void cursorControl(const CursorState::Control ctrl);

	auto mode() const { return m_mode; }

	void setCarriageReturn(const bool enable) { m_mode.set(Mode::CRLF, enable); }

	void setEcho(const bool enable) { m_mode.set(Mode::TECHO, enable); }

	void setInsertMode(const bool enable) { m_mode.set(Mode::INSERT, enable); }

	void setCursorOriginMode(const bool enable) {
		m_cursor.setUseOrigin(enable);
		// reposition cursor respective to new origin mode
		moveCursorAbsTo(topLeft());
	}

	void setAutoWrap(const bool enable) { m_mode.set(Mode::WRAP, enable); }

	/// Returns the current implicit carriage return setting as a typesafe boolean type.
	CarriageReturn carriageReturn() const { return CarriageReturn{m_mode[Mode::CRLF]}; }

	/// Moves the cursor to the next `count` tab position(s).
	void moveToNextTab(size_t count = 1);
	/// Moves the cursor to the previous `count` tab position(s).
	void moveToPrevTab(size_t count = 1);
	/// Moves the cursor the the next line (and also the first column, if set).
	void moveToNewline(const CarriageReturn cr = CarriageReturn{});

	/// Setup a tabstop at the current cursor position.
	void setTabAtCursor(const bool on_off) {
		m_tabs[m_cursor.pos.x] = on_off;
	}

	/// Inserts a marker at the current cursor position indicating a SUB control sequence.
	void showSubMarker() {
		setChar('?', m_cursor.pos);
	}

	/// Resets the cached last insert character which is used for repeatChar().
	void resetLastChar() {
		m_last_char = 0;
	}

	/// Perform a line feed operation (moving cursor down one line).
	/**
	 * If the cursor is currently at the bottom of the scroll area then
	 * this causes scrolling down by one line.
	 **/
	void doLineFeed();
	/// Perform a reverse line feed operation (moving cursor up one line).
	/**
	 * If the cursor is currently at the top of the scroll area then this
	 * causes scrolling up by one line.
	 **/
	void doReverseLineFeed();

	/// Scrolls terminal lines downwards, creating empty lines at the top.
	/**
	 * The optional origin line can be used to scroll only part of the
	 * scrolling area and keep the lines above \c origin untouched.
	 **/
	void scrollDown(int num_lines = 1, std::optional<int> origin = {});
	/// Scrolls terminal lines upwards, creating empty lines at the bottom.
	/**
	 * \see scrollDown()
	 **/
	void scrollUp(int num_lines = 1, std::optional<int> origin = {});

	/// Returns the number of characters found in the given line nr.
	int lineLen(const int y) const;
	/// Returns the number of characters found in the given line nr.
	int lineLen(const CharPos pos) const { return lineLen(pos.y); }

	/// Delete the given number of characters from the cursor position to the right.
	/**
	 * this shifts remaining characters at the end of the line to the left
	 **/
	void deleteColsAfterCursor(int count);
	/// Delete \c count lines below the current cursor position by scrolling.
	/**
	 * This only has an effect if the current cursor position is within
	 * the current scrollArea().
	 **/
	void deleteLinesBelowCursor(int count);
	/// Insert blanks after the cursor, shifting remaining characters to the right.
	void insertBlanksAfterCursor(int count);
	/// Insert \c count blank lines below the current cursor position by scrolling.
	/**
	 * This only has an effect if the current cursor position is within
	 * the current scrollArea().
	 **/
	void insertBlankLinesBelowCursor(int count);

	/// Enables or disables the terminal's UTF8 mode.
	void setUTF8(const bool on_off) {
		m_mode.set(Mode::UTF8, on_off);
	}

	/// Enables or disabled the terminal I/O file printing mode.
	void setPrintMode(const bool on_off) {
		m_mode.set(Mode::PRINT, on_off);
	}

	/// Returns whether currently the terminal I/O file printing mode is enabled.
	bool isPrintMode() const { return m_mode[Mode::PRINT]; }

	/// Performs special DEC tests triggered by escape sequence code.
	void runDECTest();

	/// Write all current lines into the I/O file.
	void dump() const {
		for (int i = 0; i < m_size.rows; i++)
			dumpLine(CharPos{0, i});
	}

	/// Write the line the cursor is on into the I/O file.
	void dumpCursorLine() const {
		dumpLine(cursor().pos);
	}

	/// Write the given line into the I/O file.
	void dumpLine(const CharPos pos) const;

	/// Returns whether any glyph currently has the BLINK attribute set.
	bool existsBlinkingGlyph() const;

	/// Sets all lines as dirty that have a Glyph matching the given attribute.
	void setDirtyByAttr(const Glyph::Attr attr);

	/// Draws the complete terminal (marking all lines dirty).
	void redraw() {
		setAllDirty();
		draw();
	}

	/// Draws all dirty lines.
	void draw();

	/// Repeats the last input character the given number of times (if printable).
	void repeatChar(int count);

	/// Provide new input data to the terminal.
	/**
	 * \param[in] show_ctrl If set then control character sequences will
	 * be shown as symbolic annotations like ^[.
	 * \return the number of processed input bytes which can be short on
	 * UTF8 decoding errors.
	 **/
	size_t write(const std::string_view data, const ShowCtrlChars show_ctrl);

	/// Returns the current cursor state of the terminal.
	const CursorState& cursor() const { return m_cursor; }

	/// Reset all cursor attrs to default.
	void resetCursorAttrs() {
		m_cursor.resetAttrs();
	}

	/// Turns on the given cursor attribute.
	void setCursorAttr(const Glyph::Attr attr) {
		m_cursor.m_attrs.mode.set(attr);
	}

	/// Turns off the given cursor attribute.
	void resetCursorAttr(const Glyph::Attr attr) {
		m_cursor.m_attrs.mode.reset(attr);
	}

	/// Sets the cursor's foreground color to be used for newly input characters.
	void setCursorFgColor(ColorIndex idx) {
		m_cursor.setFgColor(idx);
	}

	/// Sets the cursor's background color to be used for newly input characters.
	void setCursorBgColor(ColorIndex idx) {
		m_cursor.setBgColor(idx);
	}

	/// Returns the currently active scroll area.
	LineSpan scrollArea() const { return m_scroll_area; }

	auto size() const { return m_size; }
	auto numRows() const { return m_size.rows; }
	auto numCols() const { return m_size.cols; }

	auto& screen() const { return m_screen; }

	/// Report a focus change on TTY level via escape sequences.
	void reportFocus(const bool in_focus) { m_esc_handler.reportFocus(in_focus); }
	/// Report a paste event on TTY level via escape sequences.
	void reportPaste(const bool started) { m_esc_handler.reportPaste(started); }

protected: // functions

	/// Feeds the given single input rune as input.
	/**
	 * This also potentially handles control codes in which case the
	 * terminal content might not be modified.
	 **/
	void putChar(const Rune rune);

	/// (Re-)Initialize \c m_tabs and setup the default tab positions.
	void setupTabs();

	/// Resets the active scrolling area to use the whole screen.
	void resetScrollArea() {
		m_scroll_area = {0, m_size.rows - 1};
	}

	/// Draws the complete screen area.
	void drawScreen() const;

	/// Swaps from main to alternative screen and vice versa.
	void swapScreen();

	/// Place the given Rune at the given terminal position.
	void setChar(const Rune rune, const CharPos pos);
	/// Checks whether the given input Rune needs to be translated and does so if necessary.
	Rune translateChar(Rune rune) const;

	bool isCursorAtBottom() const;
	bool isCursorAtTop() const;

	/// returns a position based on \c p but at the end of the line
	CharPos atEndOfLine(const CharPos p) const {
		return CharPos{m_size.cols - 1, p.y};
	}

	CharPos topLeft() const { return {0, 0}; }
	CharPos bottomRight() const { return {m_size.cols - 1, m_size.rows - 1}; }
	bool isAtEndOfLine(const CharPos pos) {
		return pos.x >= m_size.cols - 1;
	}
	/// Returns the number of Glyph positions left in the current line with respect to the current cursor position
	int lineSpaceLeft() const {
		return m_size.cols - m_cursor.pos.x;
	}

	/// Returns the given row limited to the current screen dimensions.
	auto limitRow(const int row) { return std::clamp(row, 0, m_size.rows - 1); }
	/// Returns the given column limited to the current screen dimensions.
	auto limitCol(const int col) { return std::clamp(col, 0, m_size.cols - 1); }
	auto clampRow(int &row) { row = limitRow(row); return row; }
	auto clampCol(int &col) { col = limitCol(col); return col; }
	void clampToScreen(CharPos &c) {
		clampRow(c.y);
		clampCol(c.x);
	}
	void clamp(LineSpan &span) {
		clampRow(span.top);
		clampRow(span.bottom);
	}

	/// Returns the Glyph the cursor is currently positioned at.
	Glyph* curGlyph() { return &m_screen[m_cursor.pos]; }

protected: // data

	Selection &m_selection;
	TTY &m_tty;
	WindowSystem &m_wsys;

	TermSize m_size;    /// current terminal dimensions
	ModeBitMask m_mode; /// terminal mode flags

	CharPos m_last_cursor_pos; /// cursor position last drawn on screen
	LineSpan m_scroll_area;    /// region of lines that will be affected by scroll operations
	Rune m_last_char = 0;      /// last printed char outside of control sequence, 0 if control or otherwise unassigned

	std::array<Charset, 4> m_charsets; /// available configurable translation charsets
	size_t m_active_charset = 0;       /// current charset used from m_charsets

	CursorState m_cursor;             /// current cursor position and attributes
	CursorState m_cached_main_cursor; /// save/load cursor for main screen
	CursorState m_cached_alt_cursor;  /// ... and for alt screen

	bool m_allow_altscreen = false;  /// whether altscreen support is enabled
	EscapeHandler m_esc_handler; /// processes any kinds of terminal escape sequences
	Screen m_screen;     /// all the glyphs that make up the terminal screen
	Screen m_alt_screen; /// all the glyphs that make up the alternative terminal screen
	std::vector<bool> m_tabs;                /// marks horizontal tab positions for all lines
};

} // end ns

#endif // inc. guard
