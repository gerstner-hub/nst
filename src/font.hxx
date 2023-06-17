#ifndef NST_FONT_HXX
#define NST_FONT_HXX

// C++
#include <optional>
#include <string_view>
#include <vector>

// X11
#include <X11/Xft/Xft.h>

// X++
#include "X++/fwd.hxx"

// nst
#include "fwd.hxx"
#include "Glyph.hxx"

/**
 * @file
 *
 * Font management related types.
 **/

namespace nst {

/// Wrapper around the fontconfig slant constants.
enum class Slant : int {
	ITALIC = FC_SLANT_ITALIC,
	ROMAN = FC_SLANT_ROMAN,
	OBLIQUE = FC_SLANT_OBLIQUE
};

/// Wrapper around the fontconfig weight constants.
enum class Weight : int {
	LIGHT = FC_WEIGHT_LIGHT,
	MEDIUM = FC_WEIGHT_MEDIUM,
	DEMIBOLD = FC_WEIGHT_DEMIBOLD,
	BOLD = FC_WEIGHT_BOLD,
	BLACK = FC_WEIGHT_BLACK
};

/// Possible font attributes needed in the terminal context.
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

	/// Only wraps the given external FcPattern structure.
	/**
	 * There will be no memory management for the given pattern, the
	 * object will only serve as a better interface on the external
	 * object.
	 **/
	explicit FontPattern(FcPattern *ext) :
			m_ext_pattern{true},
			m_pattern{ext}
	{}

	~FontPattern() {
		destroy();
	}

	/// Attempts to load the given font description, returns true on success.
	bool parse(const std::string_view spec);

	bool valid() const { return m_pattern != nullptr; }

	std::optional<double> pointSize() const;

	std::optional<double> pixelSize() const;
	void setPixelSize(const double size_px);

	void setSlant(const Slant slant);
	std::optional<Slant> getSlant() const;

	void setWeight(const Weight weight);
	std::optional<Weight> getWeight() const;

	FcPattern* raw() { return m_pattern; }
	const FcPattern* raw() const { return m_pattern; }

protected: // functions

	void destroy();

	/// Generic double property retrieval from FcPattern
	std::optional<double> getDouble(const std::string_view which) const;
	/// Generic int property retrieval from FcPattern
	std::optional<int> getInt(const std::string_view which) const;

protected: // data

	/// whether m_pattern is only wrapped by us (i.e. no ownership)
	bool m_ext_pattern = false;
	FcPattern *m_pattern = nullptr;
};

/// Nst custom font management structure.
/**
 * This holds all the ingredients that make up a single usable font for
 * drawing operations.
 **/
struct Font {
public: // functions
	explicit Font(const FontFlags flags) :
			m_flags{flags}
	{}

	~Font() {
		unload();
	}
	void unload();
	bool load(const FontPattern &pattern);
	/// Try to find a pattern to display the given rune
	FcPattern* queryFontConfig(const Rune rune) const;

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
	auto flags() const { return m_flags; }

protected: // functions

	void checkSlant(const FontPattern &pattern);
	void checkWeight(const FontPattern &pattern);

protected: // data

	int m_height = 0;
	int m_width = 0;
	bool m_bad_slant = false;
	bool m_bad_weight = false;
	mutable FcFontSet *m_set = nullptr;
	XftFont *m_match = nullptr;
	FcPattern *m_pattern = nullptr;
	const FontFlags m_flags;
};

/// Simple wrapper over the Xft GlyphFontSpec struct
struct GlyphFontSpec :
		public XftGlyphFontSpec {
	void setPos(const DrawPos pos) {
		this->x = static_cast<short>(pos.x);
		this->y = static_cast<short>(pos.y);
	}
};

using GlyphFontSpecVector = std::vector<GlyphFontSpec>;

/// Manages loading, changing and properties of fonts.
class FontManager {
public: // functions

	FontManager();
	~FontManager();
	/// Sets the global font spec to be used for fonts throughout nst.
	void setFontSpec(const std::string_view font_spec) {
		m_font_spec = font_spec;
	}
	/// Loads all necessary fonts.
	bool loadFonts();
	/// Zooms all fonts by the given amount of pixels (positive/negative for zoom in/out).
	void zoom(const double val);
	/// Restore the default font size.
	void resetZoom();
	/// Returns the proper font to be used for the given Glyph mode.
	Font* fontForMode(const Glyph::AttrBitMask mode);
	/// Drops Glyph attributes in case no proper font is available for them.
	void sanitize(Glyph &g) const;

	void assignFont(const Rune rune, Font &font, GlyphFontSpec &spec);

	auto& normalFont() { return m_normal_font; }
	auto ascent() { return normalFont().ascent(); }

protected: // types

	/// Structure used for caching font lookups.
	struct FontCache {
		XftFont *font = nullptr;
		FontFlags flags;
		Rune rune;
	};

protected: // functions

	std::tuple<XftFont*, FT_UInt> lookupFontEntry(const Rune rune, Font &font);
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

/// Context used for drawing rects using font colors.
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

	/// Draw a range of GlyphFontSpec entries from a vector.
	void drawSpecs(const FontColor &color, GlyphFontSpecVector::iterator start, GlyphFontSpecVector::iterator end);

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
