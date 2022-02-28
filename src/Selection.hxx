#ifndef NST_SELECTION_HXX
#define NST_SELECTION_HXX

enum class SelectionMode {
	IDLE = 0,
	EMPTY = 1,
	READY = 2
};

class Term;

class Selection {
public: // data
	SelectionMode mode = SelectionMode::IDLE;
	int type = 0;
	int snap = 0;
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

	int alt = 0;

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
};

#endif // inc. guard
