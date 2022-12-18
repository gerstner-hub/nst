#ifndef NST_TERM_WINDOW_HXX
#define NST_TERM_WINDOW_HXX

// nst
#include "types.hxx"

namespace nst {

/* Purely graphic info */
struct TermWindow {
	Extent tty; /* tty extent (window minus border size) */
	Extent win; /* window width and height */
	Extent chr; /* single character dimensions */
	WinModeMask mode; /* window state/mode flags */
	CursorStyle cursor;

	void setCharSize(const Font &font) {
		chr.width = ceilf(font.width * config::CWSCALE);
		chr.height = ceilf(font.height * config::CHSCALE);
	}

	void setWinExtent(const Extent &ext) {
		if (ext.width != 0)
			win.width = ext.width;
		if (ext.height != 0)
			win.height = ext.height;
	}

	void setWinExtent(const TermSize &size) {
		win.width  = 2 * config::BORDERPX + size.cols * chr.width;
		win.height = 2 * config::BORDERPX + size.rows * chr.height;
	}

	//! calculates the number of characters that fit into the current
	//! terminal window
	TermSize getTermDim() const {
		auto EXTRA_PIXELS = 2 * config::BORDERPX;
		int cols = (win.width - EXTRA_PIXELS) / chr.width;
		int rows = (win.height - EXTRA_PIXELS) / chr.height;
		cols = std::max(1, cols);
		rows = std::max(1, rows);
		return TermSize{cols, rows};
	}

	void setTermDim(const TermSize &chars) {
		tty.width = chars.cols * chr.width;
		tty.height = chars.rows * chr.height;
	}

	DrawPos getDrawPos(const CharPos &cp) const {
		DrawPos dp;
		dp.x = config::BORDERPX + cp.x * chr.width;
		dp.y = config::BORDERPX + cp.y * chr.height;
		return dp;
	}

	DrawPos getNextCol(const DrawPos &pos) const {
		return DrawPos{pos.x + chr.width, pos.y};
	}

	DrawPos getNextLine(const DrawPos &pos) const {
		return DrawPos{pos.x, pos.y + chr.height};
	}

	CharPos getCharPos(const DrawPos &pos) const {
		CharPos ret{pos.x - config::BORDERPX, pos.y - config::BORDERPX};

		ret.clampX(tty.width - 1);
		ret.x /= chr.width;

		ret.clampY(tty.height - 1);
		ret.y /= chr.height;

		return ret;
	}
};

} // end ns

#endif
