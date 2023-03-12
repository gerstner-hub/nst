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

void FontColor::load(ColorIndex idx, std::string_view name) {
	if (name.empty()) {
		if (cosmos::in_range(idx, ColorIndex::START_256, ColorIndex::END_256)) { /* 256 color */
			load256(idx);
			return;
		} else {
			name = config::get_color_name(idx);
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
		auto colorname = config::get_color_name(idx);

		cosmos_throw (cosmos::RuntimeError(
				cosmos::sprintf("could not allocate color %d ('%s')", cosmos::to_integral(idx),
					colorname.empty() ? "unknown" : colorname.data())));
	}
}

void FontColor::load256(ColorIndex idx) {
	/*
	 * xterm 256 color support has the following planes:
	 *
	 *   0 -  15: the 16 standard system colors which aren't handled here
	 *  16 - 232: extended color palette organized in three sub-planes
	 * 232 - 255: extended greyscale colors
	 */

	/// this converts an xterm extended color plane to an unsigned short r/g/b component.
	auto sixd_to_16bit = [](size_t x) -> uint16_t {
		return x == 0 ? 0 : 0x3737 + 0x2828 * x;
	};

	XRenderColor tmp = { 0, 0, 0, 0xffff };

	if (idx < ColorIndex::START_GREYSCALE) { /* same colors as xterm */
		auto baseindex = cosmos::to_integral(idx - ColorIndex::START_256);
		tmp.red   = sixd_to_16bit((baseindex/36)  % 6);
		tmp.green = sixd_to_16bit((baseindex/ 6)  % 6);
		tmp.blue  = sixd_to_16bit((baseindex/ 1)  % 6);
	} else { /* greyscale */
		auto baseindex = cosmos::to_integral(idx - ColorIndex::START_GREYSCALE);
		tmp.red = 0x0808 + 0x0a0a * baseindex;
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

void RenderColor::setFromRGB(const ColorIndex rgb) {
	/* The X color values are 16-bit wide and we need to
	 * translate the one color bytes into the upper byte in the
	 * XRenderColor */
	auto raw = cosmos::to_integral(rgb);
	alpha = 0xffff;
	red = (raw & 0xff0000) >> 8;
	green = (raw & 0xff00);
	blue = (raw & 0xff) << 8;
}

} // end ns
