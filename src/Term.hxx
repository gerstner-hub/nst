#ifndef NST_TERM_HXX
#define NST_TERM_HXX

// C++
#include <array>
#include <vector>

// nst
#include "types.hxx"
#include "CSIEscape.hxx"
#include "Glyph.hxx"
#include "StringEscape.hxx"

// libcosmos
#include "cosmos/BitMask.hxx"

namespace nst {

class Selection;
class TTY;
class X11;

/// Internal representation of the screen
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
		START      = 1,
		CSI        = 2,
		STR        = 4,  /* DCS, OSC, PM, APC */
		ALTCHARSET = 8,
		STR_END    = 16, /* a final string was encountered */
		TEST       = 32, /* Enter in test mode */
		UTF8       = 64,
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
		Glyph attr; /* current char attributes */
		CharPos pos;
		StateBitMask state;
	public: // functions
		TCursor();
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

protected: // data

	Selection &m_selection;
	TTY &m_tty;
	X11 &m_x11;
	CharPos m_old_cursor_pos;
	int m_ocx = 0;            /* old cursor col */
	int m_ocy = 0;            /* old cursor row */
	std::array<Charset, 4> m_trantbl;  /* charset table translation */
	size_t m_charset = 0; /* current charset in m_trantbl */
	size_t m_icharset = 0;       /* selected charset for sequence */
	bool m_allowaltscreen = false;
	Rune m_last_char = 0;     /* last printed char outside of sequence, 0 if control */
	EscapeState m_esc_state;  /* escape state flags */
	TCursor m_cursor;         /* cursor */
	TCursor m_cached_cursors[2]; // save/load cursors for main and alt screen
	ModeBitMask m_mode;       /* terminal mode flags */
	std::vector<bool> m_tabs; // marks horizontal tab positions
	mutable std::vector<bool> m_dirty_lines; /* dirtyness of lines */
	int m_top_scroll = 0;     /* top    scroll limit */
	int m_bottom_scroll = 0;  /* bottom scroll limit */
	int m_rows = 0;           /* nb row */
	int m_cols = 0;           /* nb col */
	std::vector<Line> m_alt_screen;
	std::vector<Line> m_screen;
	STREscape m_strescseq;
	CSIEscape m_csiescseq;

public: // functions

	Term(Nst &nst);

	void init(const TermSize &tsize);

	void resize(const TermSize &new_size);

	void reset();

	void resetStringEscape() {
		m_esc_state.reset({Escape::STR_END, Escape::STR});
	}

	EscapeState& getEscapeState() { return m_esc_state; }

	void clearRegion(Range range);

	void setAllowAltScreen(bool allow) {
		m_allowaltscreen = allow;
	}

	void setDirty(int top, int bot);

	void setAllDirty() {
		setDirty(0, m_rows-1);
	}

	void clearAllTabs() {
		m_tabs.clear();
		m_tabs.resize(m_cols);
	}

	void setCharset(size_t charset) {
		m_charset = charset;
	}

	void setICharset(size_t charset) {
		m_icharset = charset;
	}

	void setScroll(int top, int bottom);

	void moveCursorTo(CharPos pos);

	void moveCursorAbsTo(CharPos pos);

	void swapScreen();

	void cursorControl(const TCursor::Control &ctrl);

	const auto& getMode() const { return m_mode; }

	void putTab(int count);
	void putNewline(bool firstcol = true);

	void setTabAtCursor(const bool on_off) {
		m_tabs[m_cursor.pos.x] = on_off;
	}

	void scrollUp(int orig, int n);
	void scrollDown(int orig, int n);

	int getLineLen(int y) const;

	/// delete the given number of characters from the cursor position to the right
	void deleteChar(int n);
	void deleteLine(int n);
	void insertBlank(int n);
	void insertBlankLine(int n);
	void setAttr(const std::vector<int> &attrs);
	void setMode(bool priv, bool set, const std::vector<int> &args);

	//! write all current lines into the I/O file
	void dump() const {
		for (size_t i = 0; i < static_cast<size_t>(m_rows); ++i)
			dumpLine(i);
	}

	//! write the given line into the I/O file
	void dumpLine(size_t n) const;

	//! returns whether any glyph currently has this attribute set
	bool testAttrSet(const Glyph::Attr &attr) const;

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

	void setPrintMode(const bool on_off) {
		m_mode.set(Mode::PRINT, on_off);
	}

	bool isPrintMode() const { return m_mode[Mode::PRINT]; }

	int bottomScrollLimit() const { return m_bottom_scroll; }
	int topScrollLimit() const { return m_top_scroll; }

	auto getNumRows() const { return m_rows; }
	auto getNumCols() const { return m_cols; }

	auto& getScreen() const { return m_screen; }

protected: // functions

	int32_t defcolor(const std::vector<int> &attr, size_t &npar);

	int32_t toTrueColor(uint r, uint g, uint b) const {
		return (1 << 24) | (r << 16) | (g << 8) | b;
	}

	/// draws the given rectangular screen region
	void drawRegion(const Range &range) const;

	void drawScreen() const { return drawRegion(Range{topLeft(), bottomRight()}); }

	void setChar(Rune u, const Glyph &attr, const CharPos &pos);
	void setDefTran(char ascii);
	void decTest(char c);
	void handleControlCode(unsigned char ascii);

	CharPos topLeft() const { return {0, 0}; }
	CharPos bottomRight() const { return {m_cols-1, m_rows-1}; }

	auto limitRow(int row) { return std::clamp(row, 0, m_rows-1); }
	auto limitCol(int col) { return std::clamp(col, 0, m_cols-1); }
	auto clampRow(int &row) { row = limitRow(row); return row; }
	auto clampCol(int &col) { col = limitCol(col); return col; }
	auto clampToScreen(CharPos &c) {
		clampRow(c.y);
		clampCol(c.x);
	}

	Glyph& getGlyphAt(const CharPos &c) { return m_screen[c.y][c.x]; }
};

} // end ns

#endif // inc. guard
