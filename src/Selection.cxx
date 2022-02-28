#include "Selection.hxx"
#include "Term.hxx"
#include "macros.hxx"

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
			alt != m_term->isSet(MODE_ALTSCREEN))
		return 0;

	if (type == Type::RECTANGULAR)
		return BETWEEN(y, nb.y, ne.y)
		    && BETWEEN(x, nb.x, ne.x);

	return BETWEEN(y, nb.y, ne.y)
	    && (y != nb.y || x >= nb.x)
	    && (y != ne.y || x <= ne.x);
}

