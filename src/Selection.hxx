#pragma once

// C++
#include <optional>
#include <set>
#include <string>

// cosmos
#include <cosmos/BitMask.hxx>

// nst
#include "fwd.hxx"
#include "types.hxx"

namespace nst {

/// This type handles the current copy/paste selection on a purely logical level (without X11 aspects).
class Selection {
public: // types

	/// Different methods for automatic selection of surrounding text.
	enum class Snap {
		NONE,    ///< don't automatically select additional text.
		WORD,    ///< try to select a complete word at the given location (based on config::WORDDELIMITERS).
		WORD_SEP ///< if the clicked-on character is itself a delimiter, look for the next same delimiter.
	};

	/// Selection context flags.
	/**
	 * These flags can change while a selection is modified, they're
	 * passed to start() and update() to indicate what the user wants.
	 **/
	enum class ContextFlag {
		BACKWARD    = 1 << 0, ///< For the Snap::WORD_SEP algorithm, look in backward direction
		ALT_SNAP    = 1 << 1, ///< Use an alternative Snap algorithm
		FINISHED    = 1 << 2, ///< The select operation is finished with this call
		RECTANGULAR = 1 << 3, ///< Select a rectangular range between start and end coordinates
		FULL_LINES  = 1 << 4, ///< Select a full line range between start and end coordinates.
	};

	using Context = cosmos::BitMask<ContextFlag>;

protected: // types

	enum class Direction {
		FORWARD,
		BACKWARD
	};

	enum class State {
		IDLE, ///< no selection process active
		EMPTY, ///< selection was started but nothing is selected yet.
		READY, ///< selection data is available.
	};

public: // functions

	explicit Selection(Nst &nst);

	// non-copyable
	Selection(const Selection &other) = delete;
	Selection& operator=(const Selection&) = delete;

	/// Removes the current selection and resets Selection state.
	void clear();

	/// Starts a new selection operation at the given start position using the given snap behaviour and context.
	void start(const CharPos pos, Snap snap, const Context ctx);

	/// Updates an active selection at/to the given position using the given type and context.
	void update(const CharPos pos, const Context ctx);

	/// returns whether the given position is part of the current selection.
	bool isSelected(const CharPos pos) const;

	/// Adjust the current selection to a scroll operation, if possible.
	/**
	 * This scrolls `num_lines` beginning at origin_y. If possible the
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

	void saveRange() {
		m_saved_orig = m_orig;
		m_saved_range = m_range;
	}

	void restoreRange() {
		m_orig = m_saved_orig;
		m_range = m_saved_range;
	}

	/// Applies any settings found in the ConfigFile settings.
	void applyConfig();

protected: // functions

	bool forceExtendSnap() const {
		return !inEmptyState() && snapActive() && m_ctx.allOf({ContextFlag::ALT_SNAP, ContextFlag::FINISHED});
	}

	/// Extends the current selection to the given position.
	/**
	 * The current selection range is changed towards the new end position
	 * `pos`. The passed in selection `type` specifies how the new
	 * selection will be calculated.
	 *
	 * \param[in] finished Whether the select operation is finished with this
	 * call (e.g. due to button release).
	 **/
	void extend(const CharPos pos);

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

	bool canExtendAny() const {
		return canExtendWord() || canExtendWordSep();
	}

	bool shouldStartNewSelection(const Snap snap, const Context ctx) const;

	bool isRectangular() const { return m_ctx[ContextFlag::RECTANGULAR]; }
	bool isFullLines()   const { return m_ctx[ContextFlag::FULL_LINES]; }
	bool isRegular()     const { return !isRectangular() && !isFullLines(); }

	bool inIdleState()   const { return m_state == State::IDLE; }
	bool inEmptyState()  const { return m_state == State::EMPTY; }
	bool inReadyState()  const { return m_state == State::READY; }

	bool snapActive() const { return m_snap != Snap::NONE; }
	bool isFinished() const { return m_ctx[ContextFlag::FINISHED]; }

	/// Recalculates the current selection after a change of m_orig.
	void recalculate();

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

	/// Attempt to extend the selection to word boundaries.
	/**
	 * \param[in,out] pos The position from which to start extending. Will
	 * be updated with the new start/end of the selection, if applicable.
	 *
	 * \param[in] delimiter If set then the word will be expanded using
	 * this delimiting character *only*. Otherwise the configured set of
	 * word delimiters is used.
	 **/
	void extendWordSnap(CharPos &pos, const Direction direction, std::optional<Rune> delimiter = std::nullopt) const;

	/// Extends the selection coordinate forwards or backwards to cover full lines.
	void extendOverLine(CharPos &pos, const Direction direction) const;

	/// Extends the selection over line breaks for the regular selection type.
	void extendLineBreaks();

	/// Checks the current selection in Snap::WORD context, whether a full URI can be selected.
	void tryURISnap();

	/// Returns whether the alt/screen was switched since start().
	bool hasScreenChanged() const;

	/// Returns whether the contained Rune is a word delimiting character
	bool isDelimiter(const Glyph &g) const;

	Direction currentSnapDir() const {
		return m_ctx[ContextFlag::BACKWARD] ? Direction::BACKWARD : Direction::FORWARD;
	}

protected: // data

	Nst &m_nst;
	Term &m_term;
	bool m_alt_screen = false; ///< alt screen setting seen when start() was invoked.
	Snap m_snap = Snap::NONE;
	Context m_ctx;
	State m_state = State::IDLE;

	Range m_range; ///< selection range with normalized coordinates
	Range m_orig; ///< selection range with original cooridinates

	Range m_saved_range; ///< selection range with normalized coordinates
	Range m_saved_orig; ///< selection range with original cooridinates

	std::wstring m_word_delimiters;
	bool m_snap_keep_newline = true;
	std::set<std::string> m_uri_schemes;
};

} // end ns
