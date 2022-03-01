// stdlib
#include <algorithm>

// nst
#include "Selection.hxx"
#include "Term.hxx"
#include "macros.hxx"
#include "nst_config.h"

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
		return BETWEEN(y, nb.y, ne.y)
		    && BETWEEN(x, nb.x, ne.x);

	return BETWEEN(y, nb.y, ne.y)
	    && (y != nb.y || x >= nb.x)
	    && (y != ne.y || x <= ne.x);
}

void Selection::start(int col, int row, Snap p_snap) {
	clear();
	mode = Selection::Mode::EMPTY;
	type = Selection::Type::REGULAR;
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
	if (type == Selection::Type::REGULAR && ob.y != oe.y) {
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
			if (!BETWEEN(newx, 0, m_term->col - 1)) {
				newy += direction;
				newx = (newx + m_term->col) % m_term->col;
				if (!BETWEEN(newy, 0, m_term->row - 1))
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

	if (BETWEEN(nb.y, orig, m_term->bot) != BETWEEN(ne.y, orig, m_term->bot)) {
		clear();
	} else if (BETWEEN(nb.y, orig, m_term->bot)) {
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
