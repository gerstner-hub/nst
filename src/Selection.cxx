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
#include "macros.hxx"
#include "nst_config.h"
#include "st.h"

using cosmos::in_range;

Selection g_sel;

Selection::Selection() {
	ob.x = -1;
	m_term = &term;
	m_tty = &g_tty;
}

void Selection::clear() {
	if (ob.x == -1)
		return;
	mode = Mode::IDLE;
	ob.x = -1;
	if (m_term) {
		m_term->setDirty(nb.y, ne.y);
	}
}

bool Selection::isSelected(int x, int y) const {
	if (mode == Mode::EMPTY || ob.x == -1 ||
			alt != m_term->getMode().test(Term::Mode::ALTSCREEN))
		return 0;

	if (type == Type::RECTANGULAR)
		return in_range(y, nb.y, ne.y)
		    && in_range(x, nb.x, ne.x);

	return in_range(y, nb.y, ne.y)
	    && (y != nb.y || x >= nb.x)
	    && (y != ne.y || x <= ne.x);
}

void Selection::start(int col, int row, Snap p_snap) {
	clear();
	mode = Mode::EMPTY;
	type = Type::REGULAR;
	alt = m_term->getMode().test(Term::Mode::ALTSCREEN);
	snap = p_snap;
	oe.x = ob.x = col;
	oe.y = ob.y = row;
	normalize();

	if (snap != Snap::NONE)
		mode = Mode::READY;

	m_term->setDirty(nb.y, ne.y);
}

void Selection::normalize(void) {
	if (type == Type::REGULAR && ob.y != oe.y) {
		nb.x = ob.y < oe.y ? ob.x : oe.x;
		ne.x = ob.y < oe.y ? oe.x : ob.x;
	} else {
		nb.x = std::min(ob.x, oe.x);
		ne.x = std::max(ob.x, oe.x);
	}
	nb.y = std::min(ob.y, oe.y);
	ne.y = std::max(ob.y, oe.y);

	checkSnap(&nb.x, &nb.y, -1);
	checkSnap(&ne.x, &ne.y, +1);

	/* expand selection over line breaks */
	if (type == Type::RECTANGULAR)
		return;
	const auto i = m_term->getLineLen(nb.y);
	if (i < nb.x)
		nb.x = i;
	if (m_term->getLineLen(ne.y) <= ne.x)
		ne.x = m_term->col - 1;
}

void Selection::checkSnap(int *x, int *y, int direction) {
	switch (snap) {
	default: break;
	case Snap::WORD: {
		/*
		 * Snap around if the word wraps around at the end or
		 * beginning of a line.
		 */
		const nst::Glyph *prevgp = &m_term->line[*y][*x];
		int prevdelim = ISDELIM(prevgp->u);
		int newx, newy, delim, xt, yt;
		while(true) {
			newx = *x + direction;
			newy = *y;
			if (!in_range(newx, 0, m_term->col - 1)) {
				newy += direction;
				newx = (newx + m_term->col) % m_term->col;
				if (!in_range(newy, 0, m_term->row - 1))
					break;

				if (direction > 0)
					yt = *y, xt = *x;
				else
					yt = newy, xt = newx;
				if (!(m_term->line[yt][xt].mode & ATTR_WRAP))
					break;
			}

			if (newx >= m_term->getLineLen(newy))
				break;

			const nst::Glyph *gp = &m_term->line[newy][newx];
			delim = ISDELIM(gp->u);
			if (!(gp->mode & ATTR_WDUMMY) && (delim != prevdelim
					|| (delim && gp->u != prevgp->u)))
				break;

			*x = newx;
			*y = newy;
			prevgp = gp;
			prevdelim = delim;
		}
		break;
	} case Snap::LINE: {
		/*
		 * Snap around if the the previous line or the current one
		 * has set ATTR_WRAP at its end. Then the whole next or
		 * previous line will be selected.
		 */
		*x = (direction < 0) ? 0 : m_term->col - 1;
		if (direction < 0) {
			for (; *y > 0; *y += direction) {
				if (!(m_term->line[*y-1][m_term->col-1].mode
						& ATTR_WRAP)) {
					break;
				}
			}
		} else if (direction > 0) {
			for (; *y < m_term->row-1; *y += direction) {
				if (!(m_term->line[*y][m_term->col-1].mode
						& ATTR_WRAP)) {
					break;
				}
			}
		}
		break;
	}}
}

void Selection::extend(int col, int row, const Type &p_type, const bool &done) {
	if (mode == Mode::IDLE)
		return;
	if (done && mode == Mode::EMPTY) {
		clear();
		return;
	}

	const auto oldey = oe.y;
	const auto oldex = oe.x;
	const auto oldsby = nb.y;
	const auto oldsey = ne.y;
	const auto oldtype = type;

	oe.x = col;
	oe.y = row;
	normalize();
	type = p_type;

	if (oldey != oe.y || oldex != oe.x || oldtype != type || mode == Mode::EMPTY)
		m_term->setDirty(std::min(nb.y, oldsby), std::max(ne.y, oldsey));

	mode = done ? Mode::IDLE : Mode::READY;
}

void Selection::scroll(int orig, int n) {
	if (ob.x == -1)
		return;

	if (in_range(nb.y, orig, m_term->bot) != in_range(ne.y, orig, m_term->bot)) {
		clear();
	} else if (in_range(nb.y, orig, m_term->bot)) {
		ob.y += n;
		oe.y += n;
		if (ob.y < m_term->top || ob.y > m_term->bot ||
		    oe.y < m_term->top || oe.y > m_term->bot) {
			clear();
		} else {
			normalize();
		}
	}
}

char* Selection::getSelection() const {
	char *str, *ptr;
	int lastx, linelen;
	const nst::Glyph *gp, *last;

	if (ob.x == -1)
		return nullptr;

	const size_t bufsize = (m_term->col+1) * (ne.y-nb.y+1) * UTF_SIZ;
	ptr = str = new char[bufsize];

	/* append every set & selected glyph to the selection */
	for (int y = nb.y; y <= ne.y; y++) {
		if ((linelen = m_term->getLineLen(y)) == 0) {
			*ptr++ = '\n';
			continue;
		}

		if (type == Type::RECTANGULAR) {
			gp = &m_term->line[y][nb.x];
			lastx = ne.x;
		} else {
			gp = &m_term->line[y][nb.y == y ? nb.x : 0];
			lastx = (ne.y == y) ? ne.x : m_term->col-1;
		}
		last = &m_term->line[y][std::min(lastx, linelen-1)];
		while (last >= gp && last->u == ' ')
			--last;

		for ( ; gp <= last; ++gp) {
			if (gp->mode & ATTR_WDUMMY)
				continue;

			ptr += utf8encode(gp->u, ptr);
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
		if ((y < ne.y || lastx >= linelen) &&
		    (!(last->mode & ATTR_WRAP) || type == Type::RECTANGULAR))
			*ptr++ = '\n';
	}
	*ptr = 0;
	return str;
}

void Selection::dump() const {
	char *ptr = getSelection();

	if (!ptr)
		return;

	m_tty->printToIoFile(ptr, strlen(ptr));
	free(ptr);
}
