// cosmos
#include "cosmos/formatting.hxx"

// X++
#include "X++/XDisplay.hxx"

// nst
#include "color.hxx"
#include "Glyph.hxx"
#include "nst_config.hxx"

namespace nst {

inline auto cmap() {
	return xpp::raw_cmap(xpp::colormap);
}

void FontColor::load(size_t colnr, std::string_view name) {
	if (name.empty()) {
		if (cosmos::in_range(colnr, 16, 255)) { /* 256 color */
			load256(colnr);
			return;
		} else {
			name = config::get_color_name(colnr);
		}
	}

	destroy();

	auto res = ::XftColorAllocName(
			xpp::display, xpp::visual, cmap(),
			name.empty() ? nullptr : name.data(), this);

	if (res == True) {
		m_loaded = true;
		return;
	} else {
		auto colorname = config::get_color_name(colnr);

		cosmos_throw (cosmos::RuntimeError(
				cosmos::sprintf("could not allocate color %zd ('%s')", colnr,
					colorname.empty() ? "unknown" : colorname.data())));
	}
}

void FontColor::load256(size_t colnr) {
	auto sixd_to_16bit = [](size_t x) -> uint16_t {
		return x == 0 ? 0 : 0x3737 + 0x2828 * x;
	};
	XRenderColor tmp = { 0, 0, 0, 0xffff };

	if (colnr < 6*6*6+16) { /* same colors as xterm */
		tmp.red   = sixd_to_16bit( ((colnr-16)/36)%6 );
		tmp.green = sixd_to_16bit( ((colnr-16)/6) %6 );
		tmp.blue  = sixd_to_16bit( ((colnr-16)/1) %6 );
	} else { /* greyscale */
		tmp.red = 0x0808 + 0x0a0a * (colnr - (6*6*6+16));
		tmp.green = tmp.blue = tmp.red;
	}

	load(tmp);
}

void FontColor::load(const XRenderColor &rc) {
	destroy();
	auto res = ::XftColorAllocValue(xpp::display, xpp::visual, cmap(), &rc, this);

	if (res == True) {
		m_loaded = true;
	} else {
		cosmos_throw (cosmos::RuntimeError("Failed to allocate color value"));
	}
}

void FontColor::destroy() {
	if (!valid())
		return;

	::XftColorFree(xpp::display, xpp::visual, cmap(), this);

	m_loaded = false;
}

void FontColor::invert() {
	RenderColor inverted{*this};
	inverted.invert();

	load(inverted);
}

void FontColor::makeFaint() {
	RenderColor faint{*this};
	faint.makeFaint();

	load(faint);
}

} // end ns
