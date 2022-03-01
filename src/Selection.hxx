#ifndef NST_SELECTION_HXX
#define NST_SELECTION_HXX

class Term;

class Selection {
public: // types

	enum class Mode {
		IDLE = 0,
		EMPTY = 1,
		READY = 2
	};

	enum class Type {
		REGULAR = 1,
		RECTANGULAR = 2
	};

	enum Snap {
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

public: // functions

	Selection() {
		ob.x = -1;
	}

	void setTerm(Term &term) {
		m_term = &term;
	}

	void clear();

	bool isSelected(int x, int y) const;
	void start(int col, int row, Snap snap);
	void extend(int col, int row, const Type &type, const bool &done);
	void scroll(int orig, int n);
	char* getSelection() const;

protected: // functions

	void normalize();
	void checkSnap(int *x, int *y, int direction);
};

#endif // inc. guard
