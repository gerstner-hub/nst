#ifndef NST_SELECTION_HXX
#define NST_SELECTION_HXX

// C++
#include <string>

// nst
#include "Glyph.hxx"
#include "types.hxx"

namespace nst {

class Term;
class TTY;
class Nst;

class Selection {
public: // types

	enum class Type : unsigned {
		REGULAR = 1,
		RECTANGULAR = 2
	};

	enum class Snap : unsigned {
		NONE = 0,
		WORD = 1,
		LINE = 2
	};

protected: // types

	enum class Mode : unsigned {
		IDLE = 0,
		EMPTY = 1,
		READY = 2
	};

protected: // data

	Term &m_term;
	TTY *m_tty = nullptr;
	bool m_alt_screen = false;
	Snap m_snap = Snap::WORD;
	Type m_type = Type::REGULAR;
	Mode m_mode = Mode::IDLE;

	/*
	 * Selection ranges:
	 * normal: normalized coordinates of the beginning and end of the selection
	 * orig: original coordinates of the beginning and end of the selection
	 */
	Range m_normal;
	Range m_orig;

public: // functions

	explicit Selection(Nst &nst);

	void clear();

	bool isSelected(const CharPos &pos) const;
	void start(int col, int row, Snap snap);
	void extend(int col, int row, const Type &type, const bool &done);
	void scroll(int orig, int n);
	std::string getSelection() const;
	//! dump current selection into I/O file
	void dump() const;

protected: // functions

	bool isRegularType() const { return m_type == Type::REGULAR; }
	bool isRectType() const { return m_type == Type::RECTANGULAR; }
	bool inIdleMode() const { return m_mode == Mode::IDLE; }
	bool inEmptyMode() const { return m_mode == Mode::EMPTY; }
	bool inReadyMode() const { return m_mode == Mode::READY; }

	void normalize();
	void checkSnap(CharPos &c, const int direction) const;
	bool isDelim(const Glyph &g) const;
};

} // end ns

#endif // inc. guard
