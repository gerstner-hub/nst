#ifndef NST_COLOR_HXX
#define NST_COLOR_HXX

// C++
#include <string_view>

// X11
#include <X11/Xft/Xft.h>

// nst
#include "Glyph.hxx"

/**
 * @file
 *
 * Font color management related types.
 **/

namespace nst {

/// Wrapper around the XftColor type which is a composite of XRenderColor and additional "pixel" info.
/**
 * The additional "pixel" info is potentially allocated from the XServer via
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

	/// reverse the color values
	void invert();

	/// make faint color of a bright color
	void makeFaint();

	bool operator==(const FontColor &other) const {
		return pixel == other.pixel &&
			color.red == other.color.red &&
			color.green == other.color.green &&
			color.blue == other.color.blue;
	}

	void load(ColorIndex idx, std::string_view name = std::string_view(""));

	void load(const XRenderColor &rc);

	bool valid() const { return m_loaded; }

	static void init();

protected: // functions

	void destroy();

	void load256(ColorIndex idx);

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

} // end ns

#endif
