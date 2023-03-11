#ifndef NST_FONTCONFIG_HXX
#define NST_FONTCONFIG_HXX

// C++
#include <optional>

// X11
#include <X11/Xft/Xft.h>

// nst
#include "Glyph.hxx"

namespace nst {

typedef Glyph::Attr Attr;

enum class Slant : int {
	ITALIC = FC_SLANT_ITALIC,
	ROMAN = FC_SLANT_ROMAN,
	OBLIQUE = FC_SLANT_OBLIQUE
};

enum class Weight : int {
	LIGHT = FC_WEIGHT_LIGHT,
	MEDIUM = FC_WEIGHT_MEDIUM,
	DEMIBOLD = FC_WEIGHT_DEMIBOLD,
	BOLD = FC_WEIGHT_BOLD,
	BLACK = FC_WEIGHT_BLACK
};

enum class FontFlags {
	NORMAL,
	ITALIC,
	BOLD,
	ITALIC_BOLD
};

/// Wrapper around a FontConfig FcPattern structure.
class FontPattern {
public: // functions
	FontPattern() = default;

	explicit FontPattern(const std::string_view spec) {
		parse(spec);
	}

	/// only wrap the given external FcPattern structure
	explicit FontPattern(FcPattern *ext) :
			m_ext_pattern{true},
			m_pattern{ext}
	{}

	~FontPattern() {
		destroy();
	}

	/// attempts to load the given font description, returns true on success.
	bool parse(const std::string_view spec);

	bool valid() const { return m_pattern != nullptr; }

	std::optional<double> pointSize() const;
	std::optional<double> pixelSize() const;
	void setPixelSize(double size_px);

	void setSlant(const Slant slant);
	void setWeight(const Weight weight);
	std::optional<Slant> getSlant() const;
	std::optional<Weight> getWeight() const;

	FcPattern* raw() { return m_pattern; }

protected: // functions

	void destroy();

protected: // data

	/// whether m_pattern is only wrapped by us (i.e. no ownership)
	bool m_ext_pattern = false;
	FcPattern *m_pattern = nullptr;
};

/// Nst custom Font management structure.
struct Font {
public: // functions
	explicit Font(const FontFlags flags) :
			m_flags{flags}
	{}

	~Font() {
		unload();
	}
	void unload();
	bool load(FontPattern &pattern);
	FcPattern* queryFontConfig(const Rune rune);

	int height() const { return m_height; }
	int width() const { return m_width; }
	int ascent() const { return m_match->ascent; }
	int descent() const { return m_match->descent; }
	bool hasBadSlant() const { return m_bad_slant; }
	bool hasBadWeight() const { return m_bad_weight; }
	XftFont* match() { return m_match; }
	const FontPattern pattern() {
		return FontPattern{m_match->pattern};
	}
	auto flags() { return m_flags; }

protected: // functions

	void checkSlant(FontPattern &pattern);
	void checkWeight(FontPattern &pattern);

protected: // data

	int m_height = 0;
	int m_width = 0;
	bool m_bad_slant = false;
	bool m_bad_weight = false;
	FcFontSet *m_set = nullptr;
	XftFont *m_match = nullptr;
	FcPattern *m_pattern = nullptr;
	const FontFlags m_flags;
};

struct RenderColor;

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

	void load(size_t colnr, std::string_view name = std::string_view(""));

	void load(const XRenderColor &rc);

	bool valid() const { return m_loaded; }

protected: // functions

	void destroy();

	void load256(size_t colnr);

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

	explicit RenderColor(const Glyph::color_t rgb) {
		setFromRGB(rgb);
	}

	explicit RenderColor(const FontColor &c) {
		static_cast<XRenderColor&>(*this) = c.color;
	}

	void setFromRGB(const Glyph::color_t rgb);

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

/// Manages loading, changing and properties of fonts.
class FontManager {
public: // functions

	FontManager();
	~FontManager();
	void setFontSpec(const std::string_view font_spec) {
		m_font_spec = font_spec;
	}
	bool loadFonts();
	void zoom(double val);
	void resetZoom();
	std::tuple<XftFont*, FT_UInt> lookupFontEntry(const Rune rune, Font &fnt);
	Font* fontForMode(const Glyph::AttrBitMask mode);
	void sanitizeColor(Glyph g) const;

	auto& normalFont() { return m_normal_font; }
	auto ascent() { return normalFont().ascent(); }

protected: // types

	struct FontCache {
		XftFont *font = nullptr;
		FontFlags flags;
		Rune rune;
	};

protected: // functions

	void unloadFonts();
	void clearCache();

protected: // data

	std::string m_font_spec;
	Font m_normal_font;
	Font m_bold_font;
	Font m_italic_font;
	Font m_italic_bold_font;
	std::optional<double> m_used_font_size; /// may differ from default size due to zooming
	std::optional<double> m_default_font_size;
	std::vector<FontCache> m_font_cache;
};

} // end ns

#endif // inc. guard
