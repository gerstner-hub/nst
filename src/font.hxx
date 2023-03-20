#ifndef NST_FONT_HXX
#define NST_FONT_HXX

// C++
#include <optional>
#include <string_view>

// X11
#include <X11/Xft/Xft.h>

// X++
#include "X++/fwd.hxx"

// nst
#include "Glyph.hxx"

/**
 * @file
 *
 * Font management related types.
 **/

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
	void sanitize(Glyph &g) const;

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

class FontDrawContext {
	FontDrawContext(const FontDrawContext &) = delete;
	FontDrawContext& operator=(const FontDrawContext&) = delete;
public: // functions
	
	FontDrawContext() = default;
	
	~FontDrawContext() {
		destroy();
	}

	void setup(xpp::XDisplay &disp, xpp::Pixmap &pixmap);

	/// Draw a rectangular font area using a starting point and extent.
	void drawRect(const FontColor &color, const DrawPos start, const Extent ext);

	void setClipRectangle(const DrawPos pos, const Extent ext);

	void resetClip();

	auto raw() { return m_ctx; }

	bool valid() { return m_ctx != nullptr; }

	void destroy();

protected: // data
	XftDraw *m_ctx = nullptr;
};

} // end ns

#endif // inc. guard
