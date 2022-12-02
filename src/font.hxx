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

/* Font Ring Cache */
enum class FRC {
	NORMAL,
	ITALIC,
	BOLD,
	ITALICBOLD
};

struct Fontcache {
	XftFont *font;
	FRC flags;
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
	XftFont *match;
	FcFontSet *set;
	FcPattern *pattern;
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

} // end ns

#endif // inc. guard
