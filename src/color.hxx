#ifndef NST_COLOR_HXX
#define NST_COLOR_HXX

// C++
#include <array>
#include <string_view>

// X11
#include <X11/Xft/Xft.h>

// nst
#include "fwd.hxx"
#include "Glyph.hxx"
#include "nst_config.hxx"

/**
 * @file
 *
 * Font color management related types.
 **/

namespace nst {

/// Returns the color name for a color number taking into account extended color configuration.
/**
 * \return
 * 	The according color name or an empty string if none is configured for the given index.
 **/
const std::string_view get_color_name(ColorIndex idx);

/// Wrapper around the XftColor type which is a composite of XRenderColor and additional "pixel" info.
/**
 * The additional "pixel" info is potentially allocated by the XServer via
 * the current colormap. Thus we need to manage this resource without creating
 * leaks or other trouble.
 **/
class FontColor :
		public XftColor {
public: // functions

	FontColor() = default;

	~FontColor() {
		destroy();
	}

	FontColor(FontColor &&other) {
		*this = std::move(other);
	}

	FontColor& operator=(FontColor &&other) {
		static_cast<XftColor&>(*this) = other;
		other.m_loaded = false;
		return *this;
	}

	FontColor(const FontColor &other) {
		*this = other;
	}

	FontColor& operator=(const FontColor &other) {
		load(other.color);
		return *this;
	}

	/// Reverse the color values.
	void invert();

	/// Make faint color of a bright color.
	void makeFaint();

	bool operator==(const FontColor &other) const {
		return pixel == other.pixel &&
			color.red == other.color.red &&
			color.green == other.color.green &&
			color.blue == other.color.blue;
	}

	void load(const ColorIndex idx, std::string_view name = std::string_view(""));

	void load(const XRenderColor &rc);

	bool valid() const { return m_loaded; }

	xpp::ColormapIndex index() const {
		return xpp::ColormapIndex{this->pixel};
	}

protected: // functions

	void destroy();

	void load256(const ColorIndex idx);

protected: // data

	bool m_loaded = false;
};

/// Wrapper around the XRenderColor primitive that adds some helper functions.
struct RenderColor :
		public XRenderColor {
public: // functions
	//
	RenderColor() = default;
	RenderColor(const RenderColor &other) = default;

	explicit RenderColor(const ColorIndex rgb) {
		setFromRGB(rgb);
	}

	explicit RenderColor(const FontColor &c) {
		static_cast<XRenderColor&>(*this) = c.color;
	}

	void setFromRGB(const ColorIndex rgb);

	void invert() {
		red   = ~red;
		green = ~green;
		blue  = ~blue;
	}

	void makeFaint() {
		red   /= 2;
		green /= 2;
		blue  /= 2;
	}
};

/// Management of the color palette and per-Glyph color settings.
class ColorManager {
public: // functions
	explicit ColorManager(TermWindow &twin) :
			m_twin{twin}
	{}

	const FontColor& defaultFront() const { return fontColor(config::DEFAULT_FG); }
	const FontColor& defaultBack() const { return fontColor(config::DEFAULT_BG); }

	const FontColor& fontColor(const ColorIndex index) const {
		return m_colors.at(cosmos::to_integral(index));
	}
	FontColor& fontColor(const ColorIndex index) {
		return m_colors.at(cosmos::to_integral(index));
	}

	/// Returns the RGB components of the given color index
	/**
	 * \return Whether a valid color index was selected and RGB have been
	 * returned in the out parameter list.
	 **/
	bool toRGB(const ColorIndex idx, uint8_t &red, uint8_t &green, uint8_t &blue) const;

	/// Assign the given name to the given color index.
	bool setColorName(const ColorIndex idx, const std::string_view name);

	void resetColors() {
		init();
	}

	void init();

	/// Adjust the current fb/bg color to the given Glyph's settings.
	void configureFor(const Glyph base);

	const FontColor& frontColor() { return m_front_color; }
	const FontColor& backColor() { return m_back_color; }
	/// Applies cursor color settings to \c glyph and returns the FontColor to be used.
	const FontColor& cursorColor(const bool is_selected, Glyph &glyph) const;

protected: // data

	TermWindow &m_twin;
	/// Current foreground color for drawing.
	FontColor m_front_color;
	/// Current background color for drawing.
	FontColor m_back_color;
	/// Colors corresponding to specific ColorIndex palette values.
	std::array<FontColor, 256UL + config::EXTENDED_COLORS.size()> m_colors;
};

} // end ns

#endif
