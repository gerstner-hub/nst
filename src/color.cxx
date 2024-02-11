// cosmos
#include "cosmos/formatting.hxx"
#include "cosmos/error/RuntimeError.hxx"

// xpp
#include "xpp/XDisplay.hxx"

// nst
#include "color.hxx"
#include "TermWindow.hxx"

namespace nst {

inline auto cmap() {
	return xpp::raw_cmap(xpp::colormap);
}

void FontColor::load(const ColorIndex idx, cosmos::SysString name) {
	if (name.empty()) {
		// 256 color range
		if (cosmos::in_range(idx, ColorIndex::START_256, ColorIndex::END_256)) {
			load256(idx);
			return;
		} else {
			name = get_color_name(idx);
		}
	}

	destroy();

	auto res = ::XftColorAllocName(
			xpp::display, xpp::visual, cmap(),
			name.raw(), this);

	if (res == True) {
		m_loaded = true;
		return;
	}

	auto colorname = get_color_name(idx);

	cosmos_throw (cosmos::RuntimeError(
			cosmos::sprintf("could not allocate color %d ('%s')", cosmos::to_integral(idx),
				colorname.empty() ? "unknown" : colorname.raw())));
}

void FontColor::load256(const ColorIndex idx) {
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

	if (idx < ColorIndex::START_GREYSCALE) { // same colors as xterm
		auto baseindex = cosmos::to_integral(idx - ColorIndex::START_256);
		tmp.red   = sixd_to_16bit((baseindex/36)  % 6);
		tmp.green = sixd_to_16bit((baseindex/ 6)  % 6);
		tmp.blue  = sixd_to_16bit((baseindex/ 1)  % 6);
	} else { // greyscale
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

void ColorManager::init() {
	for (uint32_t colnr = 0; colnr < m_colors.size(); colnr++) {
		ColorIndex idx{colnr};
		auto &fc = fontColor(idx);
		fc.load(idx);
	}
}

bool ColorManager::toRGB(const ColorIndex idx, uint8_t &red, uint8_t &green, uint8_t &blue) const {

	try {
		const auto color = fontColor(idx).color;

		red = color.red >> 8;
		green = color.green >> 8;
		blue = color.blue >> 8;
		return true;
	} catch (const std::out_of_range &) {
		return false;
	}
}

bool ColorManager::setColorName(const ColorIndex idx, const cosmos::SysString name) {
	try {
		auto &old_color = fontColor(idx);
		FontColor new_color;

		new_color.load(idx, name);
		old_color = std::move(new_color);
		return true;
	} catch (...) {
		return false;
	}
}

void ColorManager::configureFor(const Glyph base) {
	auto assignBaseColor = [this](FontColor &out, const ColorIndex color) {
		if (is_true_color(color)) {
			out.load(RenderColor{color});
		} else {
			// color is a palette index
			out = fontColor(color);
		}
	};

	assignBaseColor(m_front_color, base.fg);
	assignBaseColor(m_back_color, base.bg);

	// Change basic system colors [0-7] to bright system colors [8-15]
	if (base.needBrightColor() && base.isBasicColor()) {
		m_front_color = fontColor(base.toBrightColor());
	} else if (base.needFaintColor()) {
		m_front_color.makeFaint();
	}

	if (m_twin.inReverseMode()) {
		// NOTE: this is a bit strange logic by now which stems from
		// the fact that previously this have been pointers that
		// pointed either to fixed colors in the array or to a
		// temporary color object on the stack. For performance this
		// would certainly still be better to make this distinction,
		// but it causes ugly code ...
		// TODO: either find a nice way to implement copy-on-write, or
		// simplify this code by only calling `.invert()` right away.
		if (m_front_color == defaultFront()) {
			m_front_color = defaultBack();
		} else {
			m_front_color.invert();
		}

		if (m_back_color == defaultBack()) {
			m_back_color = defaultFront();
		} else {
			m_back_color.invert();
		}
	}

	if (base.useReverseColor()) {
		std::swap(m_front_color, m_back_color);
	}

	if (base.mode[Attr::BLINK] && m_twin.inBlinkMode()) {
		m_front_color = m_back_color;
	} else if (base.mode[Attr::INVISIBLE]) {
		m_front_color = m_back_color;
	}
}

const FontColor& ColorManager::applyCursorColor(const bool is_selected, Glyph &glyph) const {
	// Select the right color for the right mode.
	glyph.mode.limit({Attr::BOLD, Attr::ITALIC, Attr::UNDERLINE, Attr::STRUCK, Attr::WIDE});

	constexpr auto &THEME = config::THEME;

	if (m_twin.inReverseMode()) {
		glyph.setReverseColor();
		glyph.bg = THEME.fg;
		if (is_selected) {
			glyph.fg = THEME.reverse_cursor_color;
			return fontColor(THEME.cursor_color);
		} else {
			glyph.fg = THEME.cursor_color;
			return fontColor(THEME.reverse_cursor_color);
		}
	} else {
		if (is_selected) {
			glyph.fg = THEME.fg;
			glyph.bg = THEME.reverse_cursor_color;
		} else {
			if (m_twin.getCursorStyle() == CursorStyle::REVERSE_BLOCK) {
				glyph.setReverseColor();
				return fontColor(THEME.cursor_color);
			} else {
				glyph.fg = THEME.bg;
				glyph.bg = THEME.cursor_color;
			}
		}

		return fontColor(glyph.bg);
	}
}

cosmos::SysString get_color_name(const ColorIndex idx) {

	constexpr auto &THEME = config::THEME;

	if (auto raw = cosmos::to_integral(idx); raw < THEME.basic_colors.size()) {
		// returning the raw pointer here is okay since we know that
		// they're always null terminated.
		return THEME.basic_colors[raw].data();
	} else if (idx >= ColorIndex::START_EXTENDED) {
		const auto ext = cosmos::to_integral(idx - ColorIndex::START_EXTENDED);
		// check for extended colors
		if (ext < THEME.extended_colors.size())
			return THEME.extended_colors[ext].data();
	}

	// unassigned
	// NOTE: the libX functions that consume this are tolerant towards null pointers
	return {};
}

} // end ns
