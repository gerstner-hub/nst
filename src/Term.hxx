#ifndef NST_TERM_HXX
#define NST_TERM_HXX

// nst
#include "types.hxx"
#include "Glyph.hxx"

// libcosmos
#include "cosmos/BitMask.hxx"

namespace nst {

class Selection;
class TTY;

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
		int x = 0;
		int y = 0;
		StateBitMask state;
	};

public: // data

	int row = 0;            /* nb row */
	int col = 0;            /* nb col */
	Line *line = nullptr; /* screen */
	TCursor c;              /* cursor */
	int top = 0;            /* top    scroll limit */
	int bot = 0;            /* bottom scroll limit */
	ModeBitMask mode;       /* terminal mode flags */
	int *tabs = nullptr;

protected: // data

	Selection *m_selection = nullptr;
	TTY *m_tty = nullptr;
	Line *m_alt = nullptr; /* alternate screen */
	mutable int *m_dirty = nullptr;   /* dirtyness of lines */
	int m_ocx = 0;            /* old cursor col */
	int m_ocy = 0;            /* old cursor row */
	char m_trantbl[4] = {0};  /* charset table translation */
	int m_charset = 0;        /* current charset */
	int m_icharset = 0;       /* selected charset for sequence */
	bool m_allowaltscreen = false;
	Rune m_last_char = 0;    /* last printed char outside of sequence, 0 if control */
	EscapeState m_esc_state; /* escape state flags */


public: // functions

	Term() {}

	Term(int _cols, int _rows);

	void resize(int cols, int rows);

	void reset();

	void resetStringEscape() {
		m_esc_state.reset({Escape::STR_END, Escape::STR});
	}

	EscapeState& getEscapeState() { return m_esc_state; }

	void clearRegion(int x1, int y1, int x2, int y2);

	void setAllowAltScreen(bool allow) {
		m_allowaltscreen = allow;
	}

	void setDirty(int top, int bot);

	void setAllDirty() {
		setDirty(0, row-1);
	}

	void setCharset(int charset) {
		m_charset = charset;
	}

	void setICharset(int charset) {
		m_icharset = charset;
	}

	void setScroll(int t, int b);

	void moveTo(int x, int y);

	void moveAbsTo(int x, int y);

	void swapScreen();

	void cursorControl(const TCursor::Control &ctrl);

	const auto& getMode() const { return mode; }

	void putTab(int count);
	void putNewline(bool firstcol = true);

	void scrollUp(int orig, int n);
	void scrollDown(int orig, int n);

	int getLineLen(int y) const;

	void deleteChar(int n);
	void deleteLine(int n);
	void insertBlank(int n);
	void insertBlankLine(int n);
	void setAttr(const int *attr, size_t len);
	void setMode(int priv, int set, const int *args, int narg);

	//! write all current lines into the I/O file
	void dump() const {
		for (size_t i = 0; i < static_cast<size_t>(row); ++i)
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
	int write(const char *buf, int buflen, int show_ctrl);

	Rune getLastChar() const { return m_last_char; }

protected: // functions

	int32_t defcolor(const int *attr, size_t *npar, size_t len);

	int32_t toTrueColor(uint r, uint g, uint b) {
		return (1 << 24) | (r << 16) | (g << 8) | b;
	}

	void drawRegion(int x1, int y1, int x2, int  y2) const;

	void setChar(Rune u, const Glyph *attr, int x, int y);
	void setDefTran(char ascii);
	void decTest(char c);
	void handleControlCode(unsigned char ascii);
};

} // end ns

extern nst::Term term;

#endif // inc. guard
