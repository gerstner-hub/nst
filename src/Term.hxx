#ifndef NST_TERM_HXX
#define NST_TERM_HXX

// nst
#include "types.hxx"
#include "Glyph.hxx"

// libcosmos
#include "cosmos/BitMask.hxx"

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
		nst::Glyph attr; /* current char attributes */
		int x = 0;
		int y = 0;
		StateBitMask state;
	};

public: // data

	int row = 0;            /* nb row */
	int col = 0;            /* nb col */
	nst::Line *line = nullptr; /* screen */
	nst::Line *alt = nullptr; /* alternate screen */
	int *dirty = nullptr;   /* dirtyness of lines */
	TCursor c;              /* cursor */
	int ocx = 0;            /* old cursor col */
	int ocy = 0;            /* old cursor row */
	int top = 0;            /* top    scroll limit */
	int bot = 0;            /* bottom scroll limit */
	ModeBitMask mode;       /* terminal mode flags */
	int esc = 0;            /* escape state flags */
	char trantbl[4] = {0};  /* charset table translation */
	int charset = 0;        /* current charset */
	int icharset = 0;       /* selected charset for sequence */
	int *tabs = nullptr;

	nst::Rune lastc = 0;    /* last printed char outside of sequence, 0 if control */

protected: // data

	Selection *m_selection = nullptr;
	TTY *m_tty = nullptr;

public: // functions

	Term() {}

	Term(int _cols, int _rows);

	void resize(int cols, int rows);

	void reset();

	void clearRegion(int x1, int y1, int x2, int y2);

	void setDirty(int top, int bot);

	void setAllDirty() {
		setDirty(0, row-1);
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
	bool testAttrSet(const nst::Glyph::Attr &attr) const;

	//! sets all lines as dirty that have a glyph matching the given attribute
	void setDirtyByAttr(const nst::Glyph::Attr &attr);

protected: // functions

	int32_t defcolor(const int *attr, size_t *npar, size_t len);

	int32_t toTrueColor(uint r, uint g, uint b) {
		return (1 << 24) | (r << 16) | (g << 8) | b;
	}
};

extern Term term;

#endif // inc. guard
