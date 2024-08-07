#pragma once

// cosmos
#include "cosmos/BitMask.hxx"

// nst
#include "Glyph.hxx"
#include "types.hxx"

namespace nst {

/// Cursor related state.
/**
 * This contains the current logical cursor position as well as cursor
 * attributes for newly input characters and cursor specific control
 * settings.
 **/
struct CursorState {
	friend class Term;
public: // types

	/// Cursor control operations.
	enum class Control {
		SAVE, ///< save current cursor position
		LOAD ///< restore previously saved cursor position
	};

	/// Cursor runtime state flags.
	enum class State {
		WRAPNEXT = 1, ///< indicates that on next input automatic line wrap needs to occur
		ORIGIN   = 2  ///< if set then the cursor position is limited to the active scroll area
	};

	using StateBitMask = cosmos::BitMask<State>;

protected: // data

	static ColorIndex m_def_fg;
	static ColorIndex m_def_bg;
	CharPos pos;   ///< current cursor position (not yet rendered)
	Glyph m_attrs; ///< contains the currently active font attributes for newly input characters
	StateBitMask m_state;

public: // functions

	CursorState();

	void setDefaultColors(const ColorIndex fg, const ColorIndex bg) {
		m_def_fg = m_attrs.fg = fg;
		m_def_bg = m_attrs.bg = bg;
	}

	const auto& attrs() const { return m_attrs; }

	auto position() const { return pos; }

	void setFgColor(ColorIndex idx) {
		m_attrs.fg = idx;
	}

	void setBgColor(ColorIndex idx) {
		m_attrs.bg = idx;
	}

	/// Resets all rendering related attributes (colors, markup).
	void resetAttrs();

	bool needWrapNext() const { return m_state[State::WRAPNEXT]; }

	void setWrapNext(const bool on_off) {
		m_state.set(State::WRAPNEXT, on_off);
	}

	bool useOrigin() const { return m_state[State::ORIGIN]; }

	void setUseOrigin(const bool on_off) {
		m_state.set(State::ORIGIN, on_off);
	}
};

} // end ns
