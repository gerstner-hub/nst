#include "Selection.hxx"
#include "Term.hxx"
#include "macros.hxx"

void Selection::clear() {
	if (ob.x == -1)
		return;
	mode = SelectionMode::IDLE;
	ob.x = -1;
	if (m_term) {
		m_term->setDirty(nb.y, ne.y);
	}
}

bool Selection::isSelected(int x, int y) const {
	if (mode == SelectionMode::EMPTY || ob.x == -1 ||
			alt != m_term->isSet(MODE_ALTSCREEN))
		return 0;

	if (type == SEL_RECTANGULAR)
		return BETWEEN(y, nb.y, ne.y)
		    && BETWEEN(x, nb.x, ne.x);

	return BETWEEN(y, nb.y, ne.y)
	    && (y != nb.y || x >= nb.x)
	    && (y != ne.y || x <= ne.x);
}

