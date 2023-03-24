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
#include "EscapeHandler.hxx"
#include "Glyph.hxx"
#include "types.hxx"

namespace nst {

class Selection;
class TTY;
class X11;
class RuneInfo;

/// Internal representation of the screen.
/**
 * This class maintains the terminal contents on a logical level without
 * containing the actual X11 drawing logic.
 *
 * It manages the dimensions of the terminal, each line and its contents,
 * processes input data and passes escape sequences to EscapeHandler
 **/
class Term {

public: // types

	/// gobal terminal mode settings
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

	/// different terminal character sets (we don't support them all)
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

	/// cursor related data
	/**
	 * This contains the current logical cursor position as well as cursor
	 * attributes for newly inpurt characters and cursor specific control
	 * settings.
	 **/
	struct TCursor { /* "Cursor" identifier conflicts with X headers */
		friend class Term;
	public: // types

		/// cursor control operations
		enum class Control {
			SAVE, /// save current cursor position
			LOAD /// restore previously saved cursor position
		};

		/// cursor runtime state flags
		enum class State {
			WRAPNEXT = 1, /// indicates that on next input automatic line wrap needs to occur
			ORIGIN   = 2  /// if set then the cursor position is limited to the active scroll area
		};

		using StateBitMask = cosmos::BitMask<State>;

	protected: // data

		CharPos pos;   /// current cursor position (not yet rendered)
		Glyph m_attrs; /// contains the currently active font attributes for newly input characters
		StateBitMask m_state;

	public: // functions

		TCursor();

		const auto& attrs() const { return m_attrs; }

		const auto& position() const { return pos; }

		void setFgColor(ColorIndex idx) {
			m_attrs.fg = idx;
		}

		void setBgColor(ColorIndex idx) {
			m_attrs.bg = idx;
		}

		/// resets all rendering related attributes (colors, markup)
		void resetAttrs();

		bool needWrapNext() const { return m_state[State::WRAPNEXT]; }

		void setWrapNext(const bool on_off) {
			m_state.set(State::WRAPNEXT, on_off);
		}

		bool useOrigin() const { return m_state[State::ORIGIN]; }

		void setUseOrigin(const bool on_off) {
			m_state.set(State::ORIGIN, on_off);
		}
	};

protected: // data

	Selection &m_selection;
	TTY &m_tty;
	X11 &m_x11;

	TermSize m_size;    /// current terminal dimensions
	ModeBitMask m_mode; /// terminal mode flags

	CharPos m_last_cursor_pos; /// cursor position last drawn on screen
	LineSpan m_scroll_area;    /// region of lines that will be affected by scroll operations
	Rune m_last_char = 0;      /// last printed char outside of sequence, 0 if control or otherwise unassigned

	std::array<Charset, 4> m_charsets; /// available configurable translation charsets
	size_t m_active_charset = 0;       /// current charset used from m_charsets

	TCursor m_cursor;             /// current cursor position and attributes
	TCursor m_cached_main_cursor; /// save/load cursor for main screen
	TCursor m_cached_alt_cursor;  /// ... and for alt screen

	bool m_allowaltscreen = false;  /// whether altscreen support is enabled
	EscapeHandler m_esc_handler;
	Screen m_alt_screen; /// alt screen data
	Screen m_screen; /// all the glyphs that make up the terminal screen
	mutable std::vector<bool> m_dirty_lines; /// marks dirty lines
	std::vector<bool> m_tabs;                /// marks horizontal tab positions for all lines

public: // functions

	explicit Term(Nst &nst);

	void init(const Nst &nst);

	void resize(const TermSize &new_size);

	void reset();

	void clearRegion(Range range);
	/// clears the given span of lines completely
	void clearRegion(const LineSpan &span);
	void clearScreen() {
		clearRegion({topLeft(), bottomRight()});
	}

	/// clears all rows and cols after the current cursor position
	/**
	 * columns on the current line before the current cursor column will remain
	 **/
	void clearLinesBelowCursor();

	/// clears all rows and cols up to the current cursor position
	/**
	 * colums on the current line after the current cursor column will remain
	 **/
	void clearLinesAboveCursor();

	/// clears the current line up to and including the current cursor position
	void clearColsBeforeCursor();

	/// clears the current line from and including the current cursor position
	void clearColsAfterCursor();

	/// clears the line the cursor is in completely
	void clearCursorLine();

	void setDirty(LineSpan span);

	void setAllDirty() {
		setDirty(LineSpan{0, m_size.rows - 1});
	}

	void clearAllTabs() {
		m_tabs.clear();
		m_tabs.resize(m_size.cols);
	}

	void setCharset(size_t charset) {
		m_active_charset = std::clamp(charset, 0UL, m_charsets.size() - 1);
	}

	/// sets the given charset index to the given charset selection
	void setCharsetMapping(const size_t index, const Charset &cs) {
		if(index >= m_charsets.size())
			return;

		m_charsets[index] = cs;
	}

	void setScrollArea(const LineSpan &span);

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

	/// move cursor ignoring scroll limit (in ORIGIN cursor state)
	void moveCursorAbsTo(CharPos pos);

	void setAltScreen(const bool enable, const bool with_cursor);

	void cursorControl(const TCursor::Control &ctrl);

	const auto& mode() const { return m_mode; }

	void setCarriageReturn(const bool enable) { m_mode.set(Mode::CRLF, enable); }

	void setEcho(const bool enable) { m_mode.set(Mode::TECHO, enable); }

	void setInsertMode(const bool enable) { m_mode.set(Mode::INSERT, enable); }

	void setCursorOriginMode(const bool enable) {
		m_cursor.setUseOrigin(enable);
		// reposition cursor respective to new origin mode
		moveCursorAbsTo(topLeft());
	}

	void setAutoWrap(const bool enable) { m_mode.set(Mode::WRAP, enable); }

	/// returns the current implicit carriage return setting as a typesafe boolean type
	CarriageReturn carriageReturn() const { return CarriageReturn{m_mode[Mode::CRLF]}; }

	/// moves the cursor to the next `count` tab position(s)
	void moveToNextTab(size_t count = 1);
	/// moves the cursor to the previous `count` tab position(s)
	void moveToPrevTab(size_t count = 1);
	/// moves the cursor the the next line (and also the first column, if set)
	void moveToNewline(const CarriageReturn cr = CarriageReturn{});

	void setTabAtCursor(const bool on_off) {
		m_tabs[m_cursor.pos.x] = on_off;
	}

	/// inserts a marker at the current cursor position indicating a SUB control sequence
	void showSubMarker() {
		setChar('?', m_cursor.pos);
	}

	void resetLastChar() {
		m_last_char = 0;
	}

	/// perform a line feed operation (moving cursor down one line), possibly scrolling down one line
	void doLineFeed();
	/// perform a reverse line feed operation (moving cursor up one line), possibly scrolling up one line
	void doReverseLineFeed();

	/// scrolls terminal lines downwards, creating empty lines at the top
	/**
	 * The optional origin line can be used to scroll only part of the
	 * scrolling area and keep the upper lines untouched.
	 **/
	void scrollDown(int num_lines = 1, std::optional<int> origin = {});
	/// scrolls terminal lines upwards, creating empty lines at the bottom
	/**
	 * \see scrollDown()
	 **/
	void scrollUp(int num_lines = 1, std::optional<int> origin = {});

	/// returns the number of characters found in the given line nr
	int lineLen(int y) const { return lineLen(CharPos{0, y}); }
	/// returns the number of characters found in the given line position
	int lineLen(const CharPos &pos) const;

	/// delete the given number of characters from the cursor position to the right
	/**
	 * this shifts remaining characters at the end of the line to the left
	 **/
	void deleteColsAfterCursor(int count);
	void deleteLinesBelowCursor(int count);
	/// insert blanks after the cursor, shifting remaining characters to the right
	void insertBlanksAfterCursor(int count);
	void insertBlankLinesBelowCursor(int count);

	void setUTF8(const bool on_off) {
		m_mode.set(Mode::UTF8, on_off);
	}

	/// performs special DEC tests triggered by escape sequence code
	void runDECTest();

	/// write all current lines into the I/O file
	void dump() const {
		for (int i = 0; i < m_size.rows; ++i)
			dumpLine(CharPos{0, i});
	}

	/// write the line the cursor is on into the I/O file
	void dumpCursorLine() const {
		dumpLine(cursor().pos);
	}

	/// write the given line into the I/O file
	void dumpLine(const CharPos &pos) const;

	/// returns whether any glyph currently has this attribute set
	bool existsBlinkingGlyph() const;

	/// sets all lines as dirty that have a glyph matching the given attribute
	void setDirtyByAttr(const Glyph::Attr &attr);

	/// draws the complete terminal (marking all dirty)
	void redraw() {
		setAllDirty();
		draw();
	}

	/// draws only dirty lines
	void draw();

	/// repeats the last input character the given number of times (if printable)
	void repeatChar(int count);

	/// provide new data to the terminal
	size_t write(const std::string_view data, const ShowCtrlChars show_ctrl);

	const TCursor& cursor() const { return m_cursor; }

	/// reset all cursor attrs to default
	void resetCursorAttrs() {
		m_cursor.resetAttrs();
	}

	/// turn on the given cursor attribute
	void setCursorAttr(const Glyph::Attr &attr) {
		m_cursor.m_attrs.mode.set(attr);
	}

	/// turn off the given cursor attribute
	void resetCursorAttr(const Glyph::Attr &attr) {
		m_cursor.m_attrs.mode.reset(attr);
	}

	void setCursorFgColor(ColorIndex idx) {
		m_cursor.setFgColor(idx);
	}

	void setCursorBgColor(ColorIndex idx) {
		m_cursor.setBgColor(idx);
	}

	void setPrintMode(const bool on_off) {
		m_mode.set(Mode::PRINT, on_off);
	}

	bool isPrintMode() const { return m_mode[Mode::PRINT]; }

	LineSpan scrollArea() const { return m_scroll_area; }

	auto size() const { return m_size; }
	auto numRows() const { return m_size.rows; }
	auto numCols() const { return m_size.cols; }

	auto& screen() const { return m_screen; }

	void reportFocus(const bool in_focus) { m_esc_handler.reportFocus(in_focus); }
	void reportPaste(const bool started) { m_esc_handler.reportPaste(started); }

protected: // functions

	/// feeds the given single input rune as input
	/**
	 * this also potentially handles control codes without changing any
	 * actual terminal content.
	 **/
	void putChar(Rune rune);

	void resetScrollArea() {
		m_scroll_area = {0, m_size.rows - 1};
	}

	/// draws the given rectangular screen region
	void drawRegion(const Range &range) const;

	void drawScreen() const { return drawRegion(Range{topLeft(), bottomRight()}); }

	void swapScreen();

	/// place the given Rune at the given terminal position
	void setChar(Rune u, const CharPos &pos);
	/// checks whether the given input Rune needs to be translated and does so if necessary
	Rune translateChar(Rune u);

	//// returns how many columns are left after the current cursor position
	int colsLeft() const { return m_size.cols - m_cursor.pos.x; }

	bool isCursorAtBottom() const;
	bool isCursorAtTop() const;

	/// returns a position based on \c p but at the end of the line
	CharPos atEndOfLine(const CharPos &p) const {
		return CharPos{m_size.cols - 1, p.y};
	}

	CharPos topLeft() const { return {0, 0}; }
	CharPos bottomRight() const { return {m_size.cols - 1, m_size.rows - 1}; }
	bool isAtEndOfLine(const CharPos &pos) {
		return pos.x >= m_size.cols - 1;
	}
	/// returns the number of Glyph position left in the current line with respect to the current cursor position
	int lineSpaceLeft() const {
		return m_size.cols - m_cursor.pos.x;
	}

	auto limitRow(int row) { return std::clamp(row, 0, m_size.rows - 1); }
	auto limitCol(int col) { return std::clamp(col, 0, m_size.cols - 1); }
	auto clampRow(int &row) { row = limitRow(row); return row; }
	auto clampCol(int &col) { col = limitCol(col); return col; }
	auto clampToScreen(CharPos &c) {
		clampRow(c.y);
		clampCol(c.x);
	}
	auto clamp(LineSpan &span) {
		clampRow(span.top);
		clampRow(span.bottom);
	}

	Glyph* curGlyph() { return &m_screen[m_cursor.pos]; }
};

} // end ns

#endif // inc. guard
