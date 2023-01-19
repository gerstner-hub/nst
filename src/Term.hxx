#ifndef NST_TERM_HXX
#define NST_TERM_HXX

// C++
#include <array>
#include <optional>
#include <vector>

// libcosmos
#include "cosmos/BitMask.hxx"
#include "cosmos/types.hxx"

// nst
#include "CSIEscape.hxx"
#include "Glyph.hxx"
#include "StringEscape.hxx"
#include "types.hxx"

namespace nst {

class Selection;
class TTY;
class X11;
class RuneInfo;

/// Internal representation of the screen
/**
 * This class maintains the terminal contents on a logical level without
 * containing the actual X11 drawing logic.
 *
 * It manages the dimensions of the terminal, each line and its contents,
 * processes input data and therefore also escape sequences.
 **/
class Term {

public: // types

	/// gobal terminal mode settings
	enum class Mode : unsigned {
		WRAP        = 1 << 0, /// automatically wrap to next line if cursor reaches end of line
		INSERT      = 1 << 1, /// if set, on input, shift existing characters in a line to the right
		ALTSCREEN   = 1 << 2, /// if set then the alternative screen is shown
		CRLF        = 1 << 3, /// implicit carriage return on newline
		TECHO       = 1 << 4, /* ECHO conflicts with termios.h */
		PRINT       = 1 << 5, /// duplicate all input into I/O file
		UTF8        = 1 << 6, /// UTF8 input support
	};

	typedef cosmos::BitMask<Mode> ModeBitMask;

	/// escape sequence parsing status
	enum class Escape {
		START      = 1 << 0, /// \033 escape sequence started
		CSI        = 1 << 1, /// CSI escape sequence is about to be parsed
		STR        = 1 << 2, /// DCS, OSC, PM, APC
		ALTCHARSET = 1 << 3, /// requests setting an alternative character set
		STR_END    = 1 << 4, /// a final string was encountered
		TEST       = 1 << 5, /// Enter in test mode
		UTF8       = 1 << 6, /// UTF8 (character set change) requested
	};

	typedef cosmos::BitMask<Escape> EscapeState;

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

	/// cursor position related data
	/**
	 * This contains the current logical cursor position as well as cursor
	 * attributes and cursor specific control settings.
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

		typedef cosmos::BitMask<State> StateBitMask;

	protected: // data

		CharPos pos; /// current cursor position (not yet rendered)
		Glyph m_attr; /// contains the currently active font attributes for newly input characters
		StateBitMask state;

	public: // functions

		TCursor();

		/// sets new attributes like font properties or colors
		/**
		 * \param[in] attrs contains the individual sequence codes of the
		 * attribute change request.
		 **/
		bool setAttrs(const std::vector<int> &attrs);
		const auto& getAttr() const { return m_attr; }

		const auto& getPos() const { return pos; }

	protected: // functions

		/// parses the given escape sequence and returns the resulting color index to use
		/**
		 * \param[in] pos the current parse position in attrs
		 * \param[out] colidx receives the parsed color index
		 * \return the number of parsed elements in attrs at pos
		 **/
		size_t parseColor(const std::vector<int> &attrs, size_t pos, int32_t &colidx);

		int32_t toTrueColor(unsigned int r, unsigned int g, unsigned int b) const {
			return (1 << 24) | (r << 16) | (g << 8) | b;
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
	Rune m_last_char = 0;      /// last printed char outside of sequence, 0 if control

	std::array<Charset, 4> m_charsets; /// available configurable translation charsets
	size_t m_active_charset = 0;       /// current charset used from m_charsets
	size_t m_esc_charset = 0;          /// selected charset index for escape sequences

	TCursor m_cursor;             /// current cursor position and attributes
	TCursor m_cached_main_cursor; /// save/load cursor for main screen
	TCursor m_cached_alt_cursor;  /// ... and for alt screen

	bool m_allowaltscreen = false;  /// whether altscreen support is enabled
	std::vector<Line> m_alt_screen; /// alt screen data
	std::vector<Line> m_screen;     /// main screen data
	mutable std::vector<bool> m_dirty_lines; /// marks dirty lines
	std::vector<bool> m_tabs;                /// marks horizontal tab positions for all lines

	EscapeState m_esc_state; /// escape state flags
	StringEscape m_str_escape;  /// keeps track of string escape input sequences
	CSIEscape m_csi_escape;  /// keeps track of CSI escape input sequences

public: // functions

	explicit Term(Nst &nst);

	void init(const Nst &nst);

	void resize(const TermSize &new_size);

	void reset();

	void resetStringEscape() {
		m_esc_state.reset({Escape::STR_END, Escape::STR});
	}

	EscapeState& getEscapeState() { return m_esc_state; }

	void clearRegion(Range range);
	/// clears the given span of lines completely
	void clearRegion(const LineSpan &span);

	void setDirty(LineSpan span);

	void setAllDirty() {
		setDirty(LineSpan{0, m_size.rows - 1});
	}

	void clearAllTabs() {
		m_tabs.clear();
		m_tabs.resize(m_size.cols);
	}

	void setCharset(size_t charset) {
		m_active_charset = charset;
	}

	void setEscCharset(size_t charset) {
		m_esc_charset = std::clamp(charset, 0UL, m_charsets.size() - 1);
	}

	void setScrollArea(const LineSpan &span);

	void moveCursorTo(CharPos pos);

	void moveCursorAbsTo(CharPos pos);

	void swapScreen();

	void cursorControl(const TCursor::Control &ctrl);

	const auto& getMode() const { return m_mode; }

	/// moves the cursor to the next `count` tab position(s)
	void moveToNextTab(size_t count = 1);
	/// moves the cursor to the previous `count` tab position(s)
	void moveToPrevTab(size_t count = 1);
	/// moves the cursor the the next line (and also the first column, if set)
	void moveToNewline(const CarriageReturn cr = CarriageReturn());

	void setTabAtCursor(const bool on_off) {
		m_tabs[m_cursor.pos.x] = on_off;
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
	int getLineLen(int y) const { return getLineLen(CharPos{0, y}); }
	/// returns the number of characters found in the given line position
	int getLineLen(const CharPos &pos) const;

	/// delete the given number of characters from the cursor position to the right
	void deleteColsAfterCursor(int count);
	void deleteLinesBelowCursor(int count);
	void insertBlanksAfterCursor(int count);
	void insertBlankLinesBelowCursor(int count);
	void setMode(bool priv, const bool set, const std::vector<int> &args);

	/// write all current lines into the I/O file
	void dump() const {
		for (size_t i = 0; i < static_cast<size_t>(m_size.rows); ++i)
			dumpLine(i);
	}

	/// write the given line into the I/O file
	void dumpLine(const CharPos &pos) const;
	void dumpLine(int line) const { dumpLine(CharPos{0, line}); }

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

	/// initialize a newly starting terminal string escape sequence
	void initStrSequence(const StringEscape::Type &type);

	/// repeats the last input character the given number of times (if printable)
	void repeatChar(int count);

	/// provide new data to the terminal
	size_t write(const std::string_view &data, const ShowCtrlChars &show_ctrl);

	const TCursor& getCursor() const { return m_cursor; }
	bool setCursorAttrs(const std::vector<int> &attrs) {
		return m_cursor.setAttrs(attrs);
	}

	void setPrintMode(const bool on_off) {
		m_mode.set(Mode::PRINT, on_off);
	}

	bool isPrintMode() const { return m_mode[Mode::PRINT]; }

	LineSpan getScrollArea() const { return m_scroll_area; }

	const auto getSize() const { return m_size; }
	auto getNumRows() const { return m_size.rows; }
	auto getNumCols() const { return m_size.cols; }

	auto& getScreen() const { return m_screen; }

protected: // types

	using ContinueProcessing = cosmos::NamedBool<struct continue_proc_t, true>;

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

	/// place the given Rune at the given terminal position
	void setChar(Rune u, const CharPos &pos);
	/// checks whether the given input Rune needs to be translated and does so if necessary
	Rune translateChar(Rune u);
	/// on input checks whether the given Rune requires special processing
	/**
	 * \return Whether graphical processing of the input Rune should continue
	 **/
	ContinueProcessing preProcessChar(const RuneInfo &r);

	/// sets the currently selected escape charset to the given mapping
	/**
	 * \param[in] code The escape code representing the character set to
	 * map to.
	 **/
	void setCharsetMapping(const char code);
	/// performs special DEC tests triggered by escape sequence code
	void runDECTest(char code);
	/// handle the given input control code
	void handleControlCode(unsigned char code);

	void setPrivateMode(const bool set, const std::vector<int> &args);

	Line& getLine(const CharPos &pos) { return m_screen[pos.y]; }
	const Line& getLine(const CharPos &pos) const { return m_screen[pos.y]; }

	//// returns how many columns are left after the current cursor position
	int colsLeft() const { return m_size.cols - m_cursor.pos.x; }

	CharPos topLeft() const { return {0, 0}; }
	CharPos bottomRight() const { return {m_size.cols - 1, m_size.rows - 1}; }
	bool atEndOfLine(const CharPos &pos) {
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

	Glyph& getGlyphAt(const CharPos &c) { return m_screen[c.y][c.x]; }
	Glyph* getCurGlyph() { return &getGlyphAt(m_cursor.pos); }
};

} // end ns

#endif // inc. guard
