#ifndef NST_SELECTION_HXX
#define NST_SELECTION_HXX

namespace nst {

class Term;
class TTY;

class Selection {
public: // types

	enum class Mode : unsigned {
		IDLE = 0,
		EMPTY = 1,
		READY = 2
	};

	enum class Type : unsigned {
		REGULAR = 1,
		RECTANGULAR = 2
	};

	enum class Snap : unsigned {
		NONE = 0,
		WORD = 1,
		LINE = 2
	};

public: // data

	Mode mode = Mode::IDLE;
	Type type = Type::REGULAR;
	Snap snap = Snap::WORD;

	/*
	 * Selection variables:
	 * nb – normalized coordinates of the beginning of the selection
	 * ne – normalized coordinates of the end of the selection
	 * ob – original coordinates of the beginning of the selection
	 * oe – original coordinates of the end of the selection
	 */
	struct {
		int x = 0, y = 0;
	} nb, ne, ob, oe;

	bool alt = false;

protected: // data

	Term *m_term = nullptr;
	TTY *m_tty = nullptr;

public: // functions

	Selection();

	void clear();

	bool isSelected(int x, int y) const;
	void start(int col, int row, Snap snap);
	void extend(int col, int row, const Type &type, const bool &done);
	void scroll(int orig, int n);
	char* getSelection() const;
	//! dump current selection into I/O file
	void dump() const;

protected: // functions

	void normalize();
	void checkSnap(int *x, int *y, int direction);
};

} // end ns

extern nst::Selection g_sel;

#endif // inc. guard
