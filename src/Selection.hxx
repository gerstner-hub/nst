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
/**
 * Mostly the XEventHandler interacts with this type, to process mouse and
 * keyboard events related to selection handling. The type handles the
 * different selection styles, keeps the current selection range and can
 * return the text data corresponding to it, to fill the actual X selection
 * buffer with (this is done by the XSelection type).
 **/
class Selection {
public: // types

	/// Selection context flags.
	/**
	 * These flags influence the selection process and can change even
	 * during a single selection process, to indicate what the user
	 * wants.
	 **/
	enum class Flag {
		BACKWARD    = 1 << 0, ///< For SEP_SNAP mode, look in backward direction.
		ALT         = 1 << 1, ///< Use alternative logic (e.g. extend snap mode selection).
		FINISHED    = 1 << 2, ///< The select operation is finished with this call.
	};

	using Flags = cosmos::BitMask<Flag>;

	/// Different selection modes that can be used.
	enum class Mode {
		CONT_RANGE, ///< Select contiguous text between start/end coordinates (default).
		RECT_RANGE, ///< Select a rectangular region between start/end coordinates.
		LINE_RANGE, ///< Select full lines between start/end coordinates.
		WORD_SNAP,  ///< Select a word delimited by any separators at the given start coordinate.
		SEP_SNAP    ///< Select text between two word separators at the given start coordinate.
	};

public: // functions

	explicit Selection(Nst &nst);

	// non-copyable
	Selection(const Selection &other) = delete;
	Selection& operator=(const Selection&) = delete;

	/// Starts a new selection operation at the given start position using the given snap behaviour and settings.
	void start(const CharPos pos, const Mode mode, const Flags flags);

	/// Updates an active selection at/to the given position using the given type and context.
	/**
	 * \return Whether the selection process has finished.
	 **/
	bool update(const CharPos pos, const Mode mode, const Flags flags);

	/// Removes the current selection and resets Selection state.
	void reset();

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

	/// Retrieves the current selection data as an UTF8 encoded string.
	/**
	 * If nothing is currently selected then an empty string is returned.
	 **/
	std::string data() const;

	/// Dump current selection into the I/O file.
	void dump() const;

	/// Save the current selection range for later restoring.
	void saveRange() {
		m_saved_orig = m_orig;
		m_saved_range = m_range;
	}

	/// Restore the previously saved selection range.
	void restoreRange() {
		m_orig = m_saved_orig;
		m_range = m_saved_range;
	}

	/// Applies any settings found in the ConfigFile settings.
	void applyConfig();

protected: // types

	enum class Direction {
		FORWARD,
		BACKWARD
	};

	enum class State {
		IDLE,  ///< no selection process active
		EMPTY, ///< selection was started but nothing is selected yet.
		READY, ///< selection data is available, can still be updated.
	};

protected: // functions

	bool isFinished()    const { return m_flags[Flag::FINISHED]; }
	bool useAltLogic()   const { return m_flags[Flag::ALT]; }
	bool snapBackwards() const { return m_flags[Flag::BACKWARD]; }

	bool doContRange()   const { return m_mode == Mode::CONT_RANGE; }
	bool doRectRange()   const { return m_mode == Mode::RECT_RANGE; }
	bool doLineRange()   const { return m_mode == Mode::LINE_RANGE; }
	bool doWordSnap()    const { return m_mode == Mode::WORD_SNAP;  }
	bool doSepSnap()     const { return m_mode == Mode::SEP_SNAP;   }

	bool inIdleState()   const { return m_state == State::IDLE; }
	bool inEmptyState()  const { return m_state == State::EMPTY; }
	bool inReadyState()  const { return m_state == State::READY; }

	bool inSnapMode()      const { return doWordSnap() || doSepSnap(); }
	bool inRangeMode()     const { return !inSnapMode(); }
	bool allowModeChange() const { return !inSnapMode(); }

	bool existsSelection() const {
		return m_orig.isValid();
	}

	bool allowExtendSnap() const {
		return !inEmptyState() && inSnapMode() && m_flags.allOf({Flag::ALT, Flag::FINISHED});
	}

	/// Returns whether it is possible to extend a current word selection.
	bool canExtendWordSnap() const {
		return m_mode == Mode::WORD_SNAP && m_orig.isValid();
	}

	/// Returns whether it is possible to extend a current word-sep selection.
	bool canExtendSepSnap() const {
		return m_mode == Mode::SEP_SNAP && m_orig.isValid();
	}

	/// Checks whether current state allows starting a new selection process.
	bool allowNewSelection(const Mode mode, const Flags flags) const;

	/// Returns whether the alt/screen was switched since start().
	bool hasScreenChanged() const;

	/// Returns whether the contained Rune is a word delimiting character
	bool isDelimiter(const Glyph &g) const;

	Direction snapDirection() const {
		return snapBackwards() ? Direction::BACKWARD : Direction::FORWARD;
	}

	/// Calculates the current selection range a change of m_orig or other settings, for the RANGE modes.
	void calcRange();

	/// Normalize the current selection range coordinates.
	/**
	 * This function makes sure that the begin of the selection is
	 * actually a logically smaller coordinate than the end of the
	 * selection. This simplifies the rest of the selection logic which
	 * doesn't have to worry about going backwards from the start
	 * coordinate.
	 **/
	void normalizeRange();

	/// Calculates the initial snap selection for SNAP modes.
	void calcSnap();

	/// Continue the WORD_SNAP algorithm on an existing selection.
	/**
	 * \param[in] pos The position of the click event that caused this.
	 * This position influences the direction(s) in which the word snap
	 * will be performed, if possible.
	 **/
	void continueWordSnap(const CharPos pos);

	/// Continue the SEP_SNAP algorithm on an existing selection.
	void continueSepSnap();

	/// Checks the current selection in WORD_SNAP mode, whether a full URI can be selected.
	void tryURISnap();

	/// Attempt to extend the current selection to word boundaries.
	/**
	 * \param[in] delimiter If set then the word will be expanded using
	 * this delimiting character *only*. Otherwise the configured set of
	 * m_word_delimiters is used.
	 **/
	void extendWord(const Direction direction, std::optional<Rune> delimiter = std::nullopt);

	/// Extends the selection coordinate forwards or backwards to expand lines.
	void extendLine(const Direction direction);

	/// Attempt to extend from one word separator to the next.
	/**
	 * If the clicked-on characater isn't a word separator, or if there is
	 * no data to select (start/end of line reached) then \c false is
	 * returned.
	 **/
	bool extendToSep();

	/// Extends the selection over line breaks for CONT_RANGE mode.
	void extendLineBreaks();

protected: // data

	Nst &m_nst;
	Term &m_term;
	bool m_alt_screen = false; ///< alt screen setting seen when start() was invoked.
	Mode m_mode = Mode::CONT_RANGE;
	Flags m_flags;
	State m_state = State::IDLE;

	Range m_range; ///< selection range with normalized coordinates
	Range m_orig; ///< selection range with original cooridinates

	Range m_saved_range; ///< saved selection range with normalized coordinates
	Range m_saved_orig; ///< saved selection range with original cooridinates

	std::wstring m_word_delimiters;
	bool m_line_paste_keep_newline = true;
	std::set<std::string> m_uri_schemes;
};

} // end ns
