// C
#include <math.h>

// nst
#include "font.hxx"
#include "TermWindow.hxx"

namespace nst {

void TermWindow::setCharSize(const Font &font) {
	m_chr_extent.width = ceilf(font.width() * config::CW_SCALE);
	m_chr_extent.height = ceilf(font.height() * config::CH_SCALE);
}

} // end ns
