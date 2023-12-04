#ifndef NST_SELECTION_HXX
#define NST_SELECTION_HXX

// C++
#include <optional>
#include <string>

// nst
#include "fwd.hxx"
#include "types.hxx"

namespace nst {

/// This type handles the current copy/paste selection on a purely logical level (without X11 aspects).
class Selection {
public: // types

	enum class Type {
		REGULAR, /// select a range of continuous lines from start to end coordinate
		RECTANGULAR /// select a rectangular area from start to end coordinate
	};

	/// Automatic selection of surrounding text.
	enum class Snap {
		NONE, /// don't automatically select additional text
		WORD, /// try to select a complete word at the given location (based on config::WORDDELIMITERS)
		WORD_SEP, /// if the clicked-on character is itself a delimiter, look for the next same delimiter.
		LINE  /// try to select a complete line at the given location
	};

protected: // types

	enum class State {
		IDLE, /// no selection process active
		EMPTY, /// selection was started but nothing is selected yet
		READY /// selection data is available
	};

	/// for the snap algorithm this determines the direction in which to check.
	enum class Direction {
		FORWARD,
		BACKWARD
	};

public: // functions

	explicit Selection(Nst &nst);

	// non-copyable
	Selection(const Selection &other) = delete;
	Selection& operator=(const Selection&) = delete;

	/// Removes the current selection and resets Selection state.
	void clear();

	/// Starts a new selection operation at the given start position using the given snap behaviour.
	void start(const CharPos pos, const Snap snap);

	/// returns whether the given position is part of the current selection.
	bool isSelected(const CharPos pos) const;

	/// Extends the current selection to the given position.
	/**
	 * The current selection range is changed towards the new end position
	 * \c pos. The passed in selection \c type specifies how the new
	 * selection will be calculated.
	 *
	 * \param[in] done Whether the select operation is finished with this
	 * call (e.g. due to button release).
	 **/
	void extend(const CharPos pos, const Type type, const bool done);

	/// Adjust the current selection to a scroll operation, if possible.
	/**
	 * This scrolls \c num_lines beginning at origin_y. If possible the
	 * current selection will be adjusted accordingly, otherwise the
	 * selection will be cleared.
	 *
	 * \param[in] origin_y the start line to be scrolled. This is either
	 * equivalent to the current scroll area top or another line within
	 * the scroll area for scrolling only parts of the screen.
	 **/
	void scroll(const int origin_y, const int num_lines);

	/// Retrieves the content of the current selection.
	/**
	 * If nothing is currently selected then an empty string is returned.
	 **/
	std::string selection() const;

	/// Dump current selection into the I/O file.
	void dump() const;

	/// Attempt to continue the word snap algorithm on an existing selection.
	/**
	 * \param[in] pos The position of the click event that caused this.
	 * This position influences the direction(s) in which the word snap
	 * will be performed, if possible.
	 **/
	void tryContinueWordSnap(const CharPos pos);

	void tryContinueWordSepSnap();

	/// Returns whether it is possible to extend a current word selection.
	bool canExtendWord() const;

	/// Returns whether it is possible to extend a current word-sep selection.
	bool canExtendWordSep() const;

protected: // functions

	bool isRegularType() const { return m_type == Type::REGULAR; }
	bool isRectType()    const { return m_type == Type::RECTANGULAR; }

	bool inIdleState()   const { return m_state == State::IDLE; }
	bool inEmptyState()  const { return m_state == State::EMPTY; }
	bool inReadyState()  const { return m_state == State::READY; }

	/// Updates the current selection after a change of m_orig.
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

	void extendSnap();

	bool tryExtendWordSep();

	/// Attempt to extend the selection in the given direction corresponding to the current snap setting.
	/**
	 * \param[in,out] pos The position from which to start extending. Will
	 * be updated with the new start/end of the selection, if applicable.
	 **/
	void extendSnap(    CharPos &pos, const Direction direction) const;
	/// Attempt to extend the selection to word boundaries.
	/**
	 * \param[in] delimiter If set then the word will be expanded using
	 * this delimiting character *only*. Otherwise the configured set of
	 * word delimiters is used.
	 **/
	void extendWordSnap(CharPos &pos, const Direction direction, std::optional<Rune> delimiter = std::nullopt) const;
	void extendLineSnap(CharPos &pos, const Direction direction) const;

	/// Extends the selection over line breaks for the regular selection type.
	void extendLineBreaks();

	/// Returns whether the alt/screen was switched since start().
	bool hasScreenChanged() const;

protected: // data

	Nst &m_nst;
	Term &m_term;
	bool m_alt_screen = false; /// alt screen setting seen when start() was invoked.
	Snap m_snap = Snap::NONE;
	Type m_type = Type::REGULAR;
	State m_state = State::IDLE;
	bool m_force_word_extend = false;
	bool m_first_cont_extend = true;

	Range m_range; /// selection range with normalized coordinates
	Range m_orig; /// selection range with original cooridinates
};

} // end ns

#endif // inc. guard
