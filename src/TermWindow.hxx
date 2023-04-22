#ifndef NST_TERM_WINDOW_HXX
#define NST_TERM_WINDOW_HXX

// C
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

	/// Sets an absolute window size in pixels (as ported by X11).
	void setWinExtent(const Extent ext) {
		if (ext.width != 0)
			m_win_extent.width = ext.width;
		if (ext.height != 0)
			m_win_extent.height = ext.height;
	}

	/// Sets the window size in pixel based on the current configuration.
	void setWinExtent(const TermSize size) {
		m_win_extent.width  = 2 * config::BORDERPX + size.cols * m_chr_extent.width;
		m_win_extent.height = 2 * config::BORDERPX + size.rows * m_chr_extent.height;
	}

	/// Calculates the number of characters that fit into the current terminal window.
	TermSize getTermDim() const {
		constexpr auto EXTRA_PIXELS = 2 * config::BORDERPX;
		int cols = (m_win_extent.width - EXTRA_PIXELS) / m_chr_extent.width;
		int rows = (m_win_extent.height - EXTRA_PIXELS) / m_chr_extent.height;
		cols = std::max(1, cols);
		rows = std::max(1, rows);
		return TermSize{cols, rows};
	}

	/// Set terminal size in characters.
	void setTermDim(const TermSize chars) {
		m_tty_extent.width = chars.cols * m_chr_extent.width;
		m_tty_extent.height = chars.rows * m_chr_extent.height;
	}

	/// Converts a character position on the TTY into a pixel based DrawPos.
	DrawPos toDrawPos(const CharPos cp) const {
		return DrawPos{
			config::BORDERPX + cp.x * m_chr_extent.width,
			config::BORDERPX + cp.y * m_chr_extent.height
		};
	}

	/// Returns the drawing position for the next character column.
	DrawPos nextCol(const DrawPos pos) const {
		return DrawPos{pos.x + m_chr_extent.width, pos.y};
	}

	/// Returns the drawing position for the next character line.
	DrawPos nextLine(const DrawPos pos) const {
		return DrawPos{pos.x, pos.y + m_chr_extent.height};
	}

	/// Converts a pixel based drawing position into the corresponding character position.
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
	void setCursorStyle(const CursorStyle s) { m_cursor_style = s; }

	auto TTYExtent() const { return m_tty_extent; }
	auto chrExtent() const { return m_chr_extent; }
	auto winExtent() const { return m_win_extent; }

	auto mode() const { return m_mode; }

	bool checkFlag(const WinMode flag) const { return m_mode[flag]; }
	void setFlag(const WinMode flag, const bool on_off = true) { m_mode.set(flag, on_off); }
	void resetFlag(const WinMode flag) { m_mode.reset(flag); }
	void flipFlag(const WinMode flag) { m_mode.flip(flag); }

	bool inReverseMode()      const { return checkFlag(WinMode::REVERSE); }
	bool hideCursor()         const { return checkFlag(WinMode::HIDE_CURSOR); }
	bool isFocused()          const { return checkFlag(WinMode::FOCUSED); }
	bool inMouseMode()        const { return m_mode.testAny(WinMode::MOUSE); }
	bool reportMouseMotion()  const { return checkFlag(WinMode::MOUSEMOTION); }
	bool reportMouseMany()    const { return checkFlag(WinMode::MOUSEMANY); }
	bool reportMouseSGR()     const { return checkFlag(WinMode::MOUSE_SGR); }
	bool doX10Compatibility() const { return checkFlag(WinMode::MOUSEX10); }

protected: // data

	CursorStyle m_cursor_style = CursorStyle::STEADY_BLOCK;
	WinModeMask m_mode;  /// window state/mode flags
	Extent m_tty_extent; /// window minus border size
	Extent m_chr_extent; /// single character dimensions
	Extent m_win_extent; /// window width and height
};

} // end ns

#endif
