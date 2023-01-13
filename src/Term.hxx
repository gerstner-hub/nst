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

/// Internal representation of the screen
/**
 * This class maintains the terminal contents on a logical level without
 * containing the actual X11 drawing logic.
 **/
class Term {

public: // types

	enum class Mode : unsigned {
		WRAP        = 1 << 0,
		INSERT      = 1 << 1,
		ALTSCREEN   = 1 << 2,
		CRLF        = 1 << 3,
		TECHO       = 1 << 4, /* ECHO conflicts with termios.h */
		PRINT       = 1 << 5,
		UTF8        = 1 << 6,
	};

	typedef cosmos::BitMask<Mode> ModeBitMask;

	enum class Escape {
		START      = 1 << 0,
		CSI        = 1 << 1,
		STR        = 1 << 2,  /* DCS, OSC, PM, APC */
		ALTCHARSET = 1 << 3,
		STR_END    = 1 << 4, /* a final string was encountered */
		TEST       = 1 << 5, /* Enter in test mode */
		UTF8       = 1 << 6,
	};

	typedef cosmos::BitMask<Escape> EscapeState;

	struct TCursor { /* Cursor conflicts with X headers */
	public: // types
		enum class Control {
			SAVE,
			LOAD
		};
		enum class State : unsigned {
			WRAPNEXT = 1,
			ORIGIN   = 2
		};

		typedef cosmos::BitMask<State> StateBitMask;
	public: // data
		CharPos pos;
		StateBitMask state;
	protected: // data
		Glyph m_attr; /// contains the currently active font attributes for newly input characters
	public: // functions
		TCursor();
		/// sets new attributes like font properties or colors
		/**
		 * \param[in] attrs contains the individual sequence codes of the
		 * attribute change request.
		 **/
		bool setAttrs(const std::vector<int> &attrs);

		const auto& getAttr() const { return m_attr; }
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

	enum class Charset {
		GRAPHIC0,
		GRAPHIC1,
		UK,
		USA,
		MULTI,
		GER,
		FIN
	};

	using CarriageReturn = cosmos::NamedBool<struct carriage_t, true>;

protected: // data

	Nst &m_nst;
	Selection &m_selection;
	TTY &m_tty;
	X11 &m_x11;

	TermSize m_size;
	CharPos m_last_cursor_pos; /// cursor position last drawn on screen
	std::array<Charset, 4> m_trantbl;  /* charset table translation */
	size_t m_charset = 0; /* current charset in m_trantbl */
	size_t m_icharset = 0;       /* selected charset for sequence */
	bool m_allowaltscreen = false;
	Rune m_last_char = 0;     /* last printed char outside of sequence, 0 if control */
	EscapeState m_esc_state;  /* escape state flags */
	TCursor m_cursor;         /* cursor */
	TCursor m_cached_main_cursor;    // save/load cursor for main screen
	TCursor m_cached_alt_cursor;     // ... and for alt screen
	ModeBitMask m_mode;       /* terminal mode flags */
	LineSpan m_scroll_area;    /* region of lines that will be affected by scroll operations */

	std::vector<Line> m_alt_screen;
	std::vector<Line> m_screen;
	mutable std::vector<bool> m_dirty_lines;
	std::vector<bool> m_tabs; // marks horizontal tab positions

	STREscape m_strescseq;
	CSIEscape m_csiescseq;

public: // functions

	explicit Term(Nst &nst);

	void init(const TermSize &tsize);

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
		m_charset = charset;
	}

	void setICharset(size_t charset) {
		m_icharset = charset;
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

	//! write all current lines into the I/O file
	void dump() const {
		for (size_t i = 0; i < static_cast<size_t>(m_size.rows); ++i)
			dumpLine(i);
	}

	//! write the given line into the I/O file
	void dumpLine(const CharPos &pos) const;
	void dumpLine(int line) const { dumpLine(CharPos{0, line}); }

	//! returns whether any glyph currently has this attribute set
	bool existsBlinkingGlyph() const;

	//! sets all lines as dirty that have a glyph matching the given attribute
	void setDirtyByAttr(const Glyph::Attr &attr);

	void redraw() {
		setAllDirty();
		draw();
	}

	void draw();

	//! process a terminal string sequence
	void strSequence(unsigned char c);

	void putChar(Rune u);
	size_t write(const char *buf, const size_t buflen, const bool show_ctrl);

	Rune getLastChar() const { return m_last_char; }

	const TCursor& getCursor() const { return m_cursor; }
	void setCursorAttrs(const std::vector<int> &attrs) {
		if (!m_cursor.setAttrs(attrs)) {
			m_csiescseq.dump("");
		}
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

protected: // functions

	/// draws the given rectangular screen region
	void drawRegion(const Range &range) const;

	void resetScrollArea() {
		m_scroll_area = {0, m_size.rows - 1};
	}

	void drawScreen() const { return drawRegion(Range{topLeft(), bottomRight()}); }

	void setChar(Rune u, const Glyph &attr, const CharPos &pos);
	void setDefTran(char ascii);
	void decTest(char c);
	void handleControlCode(unsigned char ascii);

	void setPrivateMode(const bool set, const std::vector<int> &args);

	Line& getLine(const CharPos &pos) { return m_screen[pos.y]; }
	const Line& getLine(const CharPos &pos) const { return m_screen[pos.y]; }

	///! returns how many columns are left after the current cursor position
	int colsLeft() const { return m_size.cols - m_cursor.pos.x; }

	CharPos topLeft() const { return {0, 0}; }
	CharPos bottomRight() const { return {m_size.cols - 1, m_size.rows - 1}; }

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
};

} // end ns

#endif // inc. guard
