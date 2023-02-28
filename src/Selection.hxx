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
		REGULAR, /// select a range of continuous lines from start to end coordinate
		RECTANGULAR /// select a rectangular area from start to end coordinate
	};

	/// automatic selection or surrounding text
	enum class Snap {
		NONE, /// don't automatically select additional text
		WORD, /// try to select a complete word at the given location (based on config::WORDDELIMITERS)
		LINE  /// try to select a complete line at the given location
	};

protected: // types

	enum class State {
		IDLE, /// no selection process active
		EMPTY, /// selection was started but nothing is selected yet
		READY /// selection data is available
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

	/// extends the current selection to the given position
	/**
	 * The current selection range is changed towards the new end position
	 * \c pos. The passed in selection \c type specifies how the new
	 * selection will be caculated.
	 *
	 * \param[in] done Whether the select operation is finished with this
	 * call (e.g. due to button release).
	 **/
	void extend(const CharPos &pos, const Type type, const bool done);

	/// adjust the current selection to a scroll operation if possible
	/**
	 * This scrolls num_lines beginning at origin_y. If possible the
	 * current selection will be adjusted accordingly, otherwise the
	 * selection will be cleared.
	 *
	 * \param[in] origin_y the start line to be scrolled. This is either
	 * equivalent to the current scroll area top or another line within
	 * the scroll area for scrolling only parts of the screen.
	 **/
	void scroll(const int origin_y, const int num_lines);

	/// retrieves the content of the current selection
	/**
	 * If nothing is currently selected then an empty string is returned.
	 **/
	std::string selection() const;

	/// dump current selection into I/O file
	void dump() const;

protected: // functions

	bool isRegularType() const { return m_type == Type::REGULAR; }
	bool isRectType()    const { return m_type == Type::RECTANGULAR; }

	bool inIdleState()   const { return m_state == State::IDLE; }
	bool inEmptyState()  const { return m_state == State::EMPTY; }
	bool inReadyState()  const { return m_state == State::READY; }

	/// updates the current selection after a change of m_orig
	void update();

	/// normalize the current selection range coordinates
	/**
	 * This function makes sure that the begin of the selection is
	 * actually a logically smaller coordinate than the end of the
	 * selection. This simplifies the rest of the selection logic which
	 * doesn't have to worry about going backwards from the start
	 * coordinate.
	 **/
	void normalizeRange();

	void extendSnap() {
		extendSnap(m_range.begin, Direction::BACKWARD);
		extendSnap(m_range.end,   Direction::FORWARD);
	}
	/// attempt to extend the selection in the given direction corresponding to the current snap setting
	/**
	 * \param[in-out] pos The position from which to start extending. Will
	 * be updated with the new start/end of the selection, if applicable.
	 **/
	void extendSnap(    CharPos &pos, const Direction direction) const;
	void extendWordSnap(CharPos &pos, const Direction direction) const;
	void extendLineSnap(CharPos &pos, const Direction direction) const;

	/// extends the selection over line breaks for the regular selection type
	void extendLineBreaks();

	/// returns whether the given Glyph is a word delimiting character
	bool isDelim(const Glyph &g) const;

	/// returns whether the alt/screen was switched since start()
	bool hasScreenChanged() const;

protected: // data

	Nst &m_nst;
	Term &m_term;
	bool m_alt_screen = false; /// alt screen setting when start() was invoked
	Snap m_snap = Snap::WORD;
	Type m_type = Type::REGULAR;
	State m_state = State::IDLE;

	Range m_range; /// selection range with normalized coordinates
	Range m_orig; /// selection range with original cooridinates
};

} // end ns

#endif // inc. guard
