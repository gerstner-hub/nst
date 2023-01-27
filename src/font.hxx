#ifndef NST_FONTCONFIG_HXX
#define NST_FONTCONFIG_HXX

// C++
#include <optional>

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

struct FontCache {
	XftFont *font = nullptr;
	FontFlags flags;
	Rune unicodep;
};

/// Wrapper around a FontConfig FcPattern structure
class FontPattern {
public: // functions
	FontPattern() {}

	explicit FontPattern(const std::string &str) {
		parse(str);
	}

	/// only wrap the given external FcPattern structure
	explicit FontPattern(FcPattern *ext) :
		m_ext_pattern(true),
		m_pattern(ext) {}

	~FontPattern() {
		if (!m_ext_pattern && isValid())
			destroy();
	}

	/// attempts to load the given font description, returns true on success
	bool parse(const std::string &str);

	bool isValid() const { return m_pattern != nullptr; }

	std::optional<double> getPointSize() const;
	std::optional<double> getPixelSize() const;
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
	int height;
	int width;
	int ascent;
	int descent;
	int badslant;
	int badweight;
	short lbearing;
	short rbearing;
	XftFont *match = nullptr;
	FcFontSet *set = nullptr;
	FcPattern *pattern = nullptr;
};

struct FcPatternGuard : public cosmos::ResourceGuard<FcPattern*> {
	explicit FcPatternGuard(FcPattern *p) :
		ResourceGuard(p, [](FcPattern *_p) { FcPatternDestroy(_p); })
	{}
};
struct FcCharSetGuard : public cosmos::ResourceGuard<FcCharSet*> {
	explicit FcCharSetGuard(FcCharSet *p) :
		ResourceGuard(p, [](FcCharSet *_p) { FcCharSetDestroy(_p); })
	{}
};

typedef Glyph::Attr Attr;

class FontColor : public XftColor {
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

} // end ns

#endif // inc. guard
