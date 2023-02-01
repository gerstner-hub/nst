#ifndef NST_SELECTION_HXX
#define NST_SELECTION_HXX

// C++
#include <string>

// nst
#include "types.hxx"

namespace nst {

class Glyph;
class Nst;
class Term;

/// This type handles the current copy/paste selection on a purely logical level (without X11 aspects)
class Selection {
public: // types

	enum class Type {
		REGULAR,
		RECTANGULAR
	};

	/// automatic selection or surrounding text
	enum class Snap {
		NONE, /// don't automatically select additional text
		WORD, /// try to select a complete word at the given location (based on config::WORDDELIMITERS)
		LINE  /// try to select a complete line at the given location
	};

protected: // types

	enum class Mode {
		IDLE,
		EMPTY,
		READY
	};

	/// for the snap algorithm this determines the direction in which to check
	enum class Direction {
		FORWARD,
		BACKWARD
	};

public: // functions

	explicit Selection(Nst &nst);

	/// removes the current selection and resets Selection state
	void clear();
	/// starts a new selection operation at the given start position using the given snap behaviour
	void start(const CharPos &pos, Snap snap);

	/// returns whether the given position is part of the current selection
	bool isSelected(const CharPos &pos) const;

	void extend(int col, int row, const Type &type, const bool &done);
	void scroll(int orig, int n);
	std::string getSelection() const;
	/// dump current selection into I/O file
	void dump() const;

protected: // functions

	bool isRegularType() const { return m_type == Type::REGULAR; }
	bool isRectType()    const { return m_type == Type::RECTANGULAR; }
	bool inIdleMode()    const { return m_mode == Mode::IDLE; }
	bool inEmptyMode()   const { return m_mode == Mode::EMPTY; }
	bool inReadyMode()   const { return m_mode == Mode::READY; }

	void normalize();
	/// attempt to extend the selection in the given direction corresponding to the current snap setting
	/**
	 * \param[in-out] pos The position from which to start extending. Will
	 * be updated with the new start/end of the selection, if applicable.
	 **/
	void extendSnap(    CharPos &pos, const Direction direction) const;
	void extendWordSnap(CharPos &pos, const Direction direction) const;
	void extendLineSnap(CharPos &pos, const Direction direction) const;

	/// returns whether the given Glyph is a word delimiting character
	bool isDelim(const Glyph &g) const;

protected: // data

	Nst &m_nst;
	Term &m_term;
	bool m_alt_screen = false; /// alt screen setting when start() was invoked
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
};

} // end ns

#endif // inc. guard
