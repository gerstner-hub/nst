#ifndef NST_TERM_WINDOW_HXX
#define NST_TERM_WINDOW_HXX

// libc
#include <math.h>

// nst
#include "font.hxx"
#include "nst_config.hxx"
#include "types.hxx"

namespace nst {

/// Purely graphic info about the Terminal.
struct TermWindow {
	TermWindow() :
		m_mode{WinModeMask{WinMode::NUMLOCK}}
	{}

	void setCharSize(const Font &font) {
		m_chr_extent.width = ceilf(font.width() * config::CW_SCALE);
		m_chr_extent.height = ceilf(font.height() * config::CH_SCALE);
	}

	void setWinExtent(const Extent ext) {
		if (ext.width != 0)
			m_win_extent.width = ext.width;
		if (ext.height != 0)
			m_win_extent.height = ext.height;
	}

	void setWinExtent(const TermSize size) {
		m_win_extent.width  = 2 * config::BORDERPX + size.cols * m_chr_extent.width;
		m_win_extent.height = 2 * config::BORDERPX + size.rows * m_chr_extent.height;
	}

	/// calculates the number of characters that fit into the current terminal window
	TermSize getTermDim() const {
		auto EXTRA_PIXELS = 2 * config::BORDERPX;
		int cols = (m_win_extent.width - EXTRA_PIXELS) / m_chr_extent.width;
		int rows = (m_win_extent.height - EXTRA_PIXELS) / m_chr_extent.height;
		cols = std::max(1, cols);
		rows = std::max(1, rows);
		return TermSize{cols, rows};
	}

	void setTermDim(const TermSize chars) {
		m_tty_extent.width = chars.cols * m_chr_extent.width;
		m_tty_extent.height = chars.rows * m_chr_extent.height;
	}

	DrawPos toDrawPos(const CharPos cp) const {
		DrawPos dp;
		dp.x = config::BORDERPX + cp.x * m_chr_extent.width;
		dp.y = config::BORDERPX + cp.y * m_chr_extent.height;
		return dp;
	}

	DrawPos nextCol(const DrawPos pos) const {
		return DrawPos{pos.x + m_chr_extent.width, pos.y};
	}

	DrawPos nextLine(const DrawPos pos) const {
		return DrawPos{pos.x, pos.y + m_chr_extent.height};
	}

	CharPos toCharPos(const DrawPos pos) const {
		CharPos ret{pos.x - config::BORDERPX, pos.y - config::BORDERPX};

		ret.clampX(m_tty_extent.width - 1);
		ret.x /= m_chr_extent.width;

		ret.clampY(m_tty_extent.height - 1);
		ret.y /= m_chr_extent.height;

		return ret;
	}

	auto activeForegroundColor() const {
		return m_mode[WinMode::REVERSE] ? config::DEFAULT_FG : config::DEFAULT_BG;
	}

	auto getCursorStyle() const { return m_cursor_style; }
	void setCursorStyle(const CursorStyle &s) { m_cursor_style = s; }

	const auto& TTYExtent() const { return m_tty_extent; }
	const auto& chrExtent() const { return m_chr_extent; }
	const auto& winExtent() const { return m_win_extent; }

	const auto& mode() const { return m_mode; }
	bool checkFlag(const WinMode &flag) const { return m_mode[flag]; }
	void setFlag(const WinMode &flag, const bool on_off = true) { m_mode.set(flag, on_off); }
	void resetFlag(const WinMode &flag) { m_mode.reset(flag); }
	void flipFlag(const WinMode &flag) { m_mode.flip(flag); }

	bool inReverseMode() const { return checkFlag(WinMode::REVERSE); }
	bool hideCursor() const { return checkFlag(WinMode::HIDE_CURSOR); }
	bool isFocused() const { return checkFlag(WinMode::FOCUSED); }

protected: // data

	CursorStyle m_cursor_style = CursorStyle::STEADY_BLOCK;
	WinModeMask m_mode;  /// window state/mode flags
	Extent m_tty_extent; /// (window minus border size)
	Extent m_chr_extent; /// single character dimensions
	Extent m_win_extent; /// window width and height
};

} // end ns

#endif
