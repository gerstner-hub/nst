#ifndef NST_FONTCONFIG_HXX
#define NST_FONTCONFIG_HXX

// C++
#include <optional>

// X11
#include <X11/Xft/Xft.h>

// cosmos
#include "cosmos/types.hxx"

// nst
#include "Glyph.hxx"

namespace nst {

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
	ITALICBOLD
};

/// Wrapper around a FontConfig FcPattern structure
class FontPattern {
public: // functions
	FontPattern() = default;

	explicit FontPattern(const std::string &str) {
		parse(str);
	}

	/// only wrap the given external FcPattern structure
	explicit FontPattern(FcPattern *ext) :
			m_ext_pattern{true},
			m_pattern{ext}
	{}

	~FontPattern() {
		if (!m_ext_pattern && isValid())
			destroy();
	}

	/// attempts to load the given font description, returns true on success
	bool parse(const std::string &str);

	bool isValid() const { return m_pattern != nullptr; }

	std::optional<double> pointSize() const;
	std::optional<double> pixelSize() const;
	void setPixelSize(double size_px);

	void setSlant(const Slant &slant);
	void setWeight(const Weight &weight);

	FcPattern* raw() { return m_pattern; }

protected: // functions

	void destroy();

protected: // data

	/// whether m_pattern is only wrapped by us (i.e. no ownership)
	bool m_ext_pattern = false;
	FcPattern *m_pattern = nullptr;
};

/* Font structure */
struct Font {
	int height = 0;
	int width = 0;
	int ascent = 0;
	int descent = 0;
	bool badslant = false;
	bool badweight = false;
	short lbearing = 0;
	short rbearing = 0;
	XftFont *match = nullptr;
	FcFontSet *set = nullptr;
	FcPattern *pattern = nullptr;
public: // functions
	~Font() {
		unload();
	}
	void unload();
	bool load(FontPattern &pattern);
	FcPattern* queryFontConfig(const Rune rune);
};

struct FcPatternGuard :
		public cosmos::ResourceGuard<FcPattern*> {
	explicit FcPatternGuard(FcPattern *p) :
		ResourceGuard{p, [](FcPattern *_p) { ::FcPatternDestroy(_p); }}
	{}
};
struct FcCharSetGuard :
		public cosmos::ResourceGuard<FcCharSet*> {
	explicit FcCharSetGuard(FcCharSet *p) :
		ResourceGuard{p, [](FcCharSet *_p) { ::FcCharSetDestroy(_p); }}
	{}
};

typedef Glyph::Attr Attr;

class FontColor :
		public XftColor {
public:
	void invert() {
		color.red = ~color.red;
		color.green = ~color.green;
		color.blue = ~color.blue;
	}

	FontColor inverted() const {
		auto ret = FontColor(*this);
		ret.invert();
		return ret;
	}

	void makeFaint() {
		color.red /= 2;
		color.green /= 2;
		color.blue /= 2;
	}

	FontColor faint() const {
		auto ret = FontColor(*this);
		ret.makeFaint();
		return ret;
	}

	void assignTo(XRenderColor &xc) const {
		xc.red = color.red;
		xc.green = color.green;
		xc.blue = color.blue;
		xc.alpha = color.alpha;
	}

	bool operator==(const FontColor &other) const {
		return pixel == other.pixel &&
			color.red == other.color.red &&
			color.green == other.color.green &&
			color.blue == other.color.blue;
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
	std::tuple<XftFont*, FT_UInt> lookupFontEntry(const Rune rune, Font &fnt, const FontFlags flags);
	std::tuple<Font*, FontFlags> fontForMode(const Glyph::AttrBitMask &mode);
	void sanitizeColor(Glyph g) const;

	auto& normalFont() { return m_normal_font; }
	auto ascent() { return normalFont().ascent; }

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
	Font m_italics_font;
	Font m_italics_bold_font;
	std::optional<double> m_used_font_size; /// may differ from default size due to zooming
	std::optional<double> m_default_font_size;
	std::vector<FontCache> m_font_cache;
};

/// Wrapper around the XRenderColor primitive that adds some helper functions
struct RenderColor :
		public XRenderColor {
public: // functions
	//
	RenderColor() = default;

	explicit RenderColor(const Glyph::color_t rgb) {
		setFromRGB(rgb);
	}

	explicit RenderColor(const FontColor &c) {
		c.assignTo(*this);
	}

	void setFromRGB(const Glyph::color_t rgb);
};

} // end ns

#endif // inc. guard
