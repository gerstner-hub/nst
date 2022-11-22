// stdlib
#include <algorithm>

// libc
#include <string.h>

// cosmos
#include "cosmos/algs.hxx"

// nst
#include "Selection.hxx"
#include "Term.hxx"
#include "TTY.hxx"
#include "codecs.hxx"
#include "nst.hxx"
#include "nst_config.h"

using cosmos::in_range;

namespace nst {

typedef Glyph::Attr Attr;

Selection::Selection(Term &term) : m_term(term) {
	m_orig.invalidate();
	m_tty = &Nst::getTTY();
}

void Selection::clear() {
	if (!m_orig.isValid())
		return;

	m_mode = Mode::IDLE;
	m_orig.invalidate();
	m_term.setDirty(m_normal.begin.y, m_normal.end.y);
}

bool Selection::isSelected(int x, int y) const {
	if (inEmptyMode() || !m_orig.isValid() || m_alt_screen != m_term.getMode().test(Term::Mode::ALTSCREEN))
		return 0;
	else if (isRectType())
		return in_range(y, m_normal.begin.y, m_normal.end.y) && in_range(x, m_normal.begin.x, m_normal.end.x);
	else
		return in_range(y, m_normal.begin.y, m_normal.end.y) &&
			(y != m_normal.begin.y || x >= m_normal.begin.x) &&
			(y != m_normal.end.y || x <= m_normal.end.x);
}

void Selection::start(int col, int row, Snap snap) {
	clear();
	m_mode = Mode::EMPTY;
	m_type = Type::REGULAR;
	m_alt_screen = m_term.getMode().test(Term::Mode::ALTSCREEN);
	m_snap = snap;
	m_orig.begin.set(col, row);
	m_orig.end.set(col, row);
	normalize();

	if (m_snap != Snap::NONE)
		m_mode = Mode::READY;

	m_term.setDirty(m_normal.begin.y, m_normal.end.y);
}

void Selection::normalize(void) {
	if (isRegularType() && m_orig.begin.y != m_orig.end.y) {
		m_normal.begin.x = m_orig.begin.y < m_orig.end.y ? m_orig.begin.x : m_orig.end.x;
		m_normal.end.x   = m_orig.begin.y < m_orig.end.y ? m_orig.end.x   : m_orig.begin.x;
	} else {
		m_normal.begin.x = std::min(m_orig.begin.x, m_orig.end.x);
		m_normal.end.x   = std::max(m_orig.begin.x, m_orig.end.x);
	}

	m_normal.begin.y = std::min(m_orig.begin.y, m_orig.end.y);
	m_normal.end.y   = std::max(m_orig.begin.y, m_orig.end.y);

	checkSnap(m_normal.begin, -1);
	checkSnap(m_normal.end,   +1);

	/* expand selection over line breaks */
	if (isRectType())
		return;
	const auto len = m_term.getLineLen(m_normal.begin.y);
	m_normal.begin.x = std::min(m_normal.begin.x, len);
	if (m_term.getLineLen(m_normal.end.y) <= m_normal.end.x)
		m_normal.end.x = m_term.getNumCols() - 1;
}

void Selection::checkSnap(Coord &c, const int direction) const {
	const auto &screen = m_term.getScreen();

	switch (m_snap) {
	default: break;
	case Snap::WORD: {
		/*
		 * Snap around if the word wraps around at the end or beginning of a line.
		 */
		const Glyph *prevgp = &screen[c.y][c.x];
		int prevdelim = isDelim(*prevgp);
		Coord newc;
		Coord t;
		int delim;
		while(true) {
			newc.set(c.x + direction, c.y);
			const auto tcols = m_term.getNumCols();
			if (!in_range(newc.x, 0, tcols - 1)) {
				newc.y += direction;
				newc.x = (newc.x + tcols) % tcols;
				if (!in_range(newc.y, 0, m_term.getNumRows() - 1))
					break;

				if (direction > 0)
					t = c;
				else
					t = newc;
				if (!(screen[t.y][t.x].mode[Attr::WRAP]))
					break;
			}

			if (newc.x >= m_term.getLineLen(newc.y))
				break;

			const Glyph *gp = &screen[newc.y][newc.x];
			delim = isDelim(*gp);
			if (!(gp->mode[Attr::WDUMMY]) &&
				(delim != prevdelim || (delim && gp->u != prevgp->u)))
				break;

			c = newc;
			prevgp = gp;
			prevdelim = delim;
		}
		break;
	} case Snap::LINE: {
		const auto tcols = m_term.getNumCols();
		/*
		 * Snap around if the the previous line or the current one
		 * has set WRAP at its end. Then the whole next or previous
		 * line will be selected.
		 */
		c.x = (direction < 0) ? 0 : tcols - 1;
		if (direction < 0) {
			for (; c.y > 0; c.y += direction) {
				if (!(screen[c.y-1][tcols-1].mode[Attr::WRAP])) {
					break;
				}
			}
		} else if (direction > 0) {
			for (; c.y < m_term.getNumRows()-1; c.y += direction) {
				if (!(screen[c.y][tcols-1].mode[Attr::WRAP])) {
					break;
				}
			}
		}
		break;
	} // case
	} // switch
}

void Selection::extend(int col, int row, const Type &type, const bool &done) {
	if (inIdleMode())
		return;
	else if (done && inEmptyMode()) {
		clear();
		return;
	}

	const Coord old_end = m_orig.end;
	const auto oldsby = m_normal.begin.y;
	const auto oldsey = m_normal.end.y;
	const auto oldtype = m_type;

	m_orig.end.set(col, row);
	normalize();
	m_type = type;

	if (old_end.y != m_orig.end.y || old_end.x != m_orig.end.x || oldtype != m_type || inEmptyMode())
		m_term.setDirty(std::min(m_normal.begin.y, oldsby), std::max(m_normal.end.y, oldsey));

	m_mode = done ? Mode::IDLE : Mode::READY;
}

void Selection::scroll(int orig, int n) {
	if (!m_orig.isValid())
		return;

	if (in_range(m_normal.begin.y, orig, m_term.bottomScrollLimit()) != in_range(m_normal.end.y, orig, m_term.bottomScrollLimit())) {
		clear();
	} else if (in_range(m_normal.begin.y, orig, m_term.bottomScrollLimit())) {
		m_orig.begin.y += n;
		m_orig.end.y += n;
		if (m_orig.begin.y < m_term.topScrollLimit() || m_orig.begin.y > m_term.bottomScrollLimit() ||
		    m_orig.end.y < m_term.topScrollLimit() || m_orig.end.y > m_term.bottomScrollLimit()) {
			clear();
		} else {
			normalize();
		}
	}
}

std::string Selection::getSelection() const {
	if (!m_orig.isValid())
		return "";

	const auto &screen = m_term.getScreen();
	const size_t bufsize = (m_term.getNumCols()+1) * (m_normal.end.y - m_normal.begin.y+1) * utf8::UTF_SIZE;
	std::string ret;
	const Glyph *gp, *last;
	int lastx, linelen;

	ret.reserve(bufsize);

	/* append every set & selected glyph to the selection */
	for (int y = m_normal.begin.y; y <= m_normal.end.y; y++) {
		if ((linelen = m_term.getLineLen(y)) == 0) {
			ret.push_back('\n');
			continue;
		}

		if (isRectType()) {
			gp = &screen[y][m_normal.begin.x];
			lastx = m_normal.end.x;
		} else {
			gp = &screen[y][m_normal.begin.y == y ? m_normal.begin.x : 0];
			lastx = (m_normal.end.y == y) ? m_normal.end.x : m_term.getNumCols() - 1;
		}
		last = &screen[y][std::min(lastx, linelen-1)];
		while (last >= gp && last->u == ' ')
			--last;

		for ( ; gp <= last; ++gp) {
			if (gp->mode.test(Attr::WDUMMY))
				continue;

			utf8::encode(gp->u, ret);
		}

		/*
		 * Copy and pasting of line endings is inconsistent
		 * in the inconsistent terminal and GUI world.
		 * The best solution seems like to produce '\n' when
		 * something is copied from st and convert '\n' to
		 * '\r', when something to be pasted is received by
		 * st.
		 * FIXME: Fix the computer world.
		 */
		if ((y < m_normal.end.y || lastx >= linelen) && (!(last->mode[Attr::WRAP]) || isRectType()))
			ret.push_back('\n');
	}

	return ret;
}

void Selection::dump() const {
	auto selection = getSelection();

	if (selection.empty())
		return;

	m_tty->printToIoFile(selection.c_str(), selection.length());
}

} // end ns
