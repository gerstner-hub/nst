// C++
#include <iostream>
#include <string>

// X11
#include <X11/Xft/Xft.h>

// cosmos
#include "cosmos/algs.hxx"
#include "cosmos/error/RuntimeError.hxx"
#include "cosmos/types.hxx"


// X++
#include "X++/XDisplay.hxx"

// nst
#include "font.hxx"
#include "nst_config.hxx"

namespace {

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

} // end ns

namespace nst {

bool FontPattern::parse(const std::string_view spec) {
	destroy();

	if (spec[0] == '-')
		m_pattern = XftXlfdParse(spec.data(), False, False);
	else
		m_pattern = FcNameParse((const FcChar8 *)spec.data());

	return valid();
}

void FontPattern::setPixelSize(double size_px) {
	FcPatternDel(m_pattern, FC_PIXEL_SIZE);
	FcPatternDel(m_pattern, FC_SIZE);
	FcPatternAddDouble(m_pattern, FC_PIXEL_SIZE, size_px);
}

std::optional<double> FontPattern::pointSize() const {
	if(!m_pattern)
		return {};

	double ret;
	auto res = FcPatternGetDouble(m_pattern, FC_SIZE, 0, &ret);
	if (res == FcResultMatch)
	       return ret;
	return {};
}

std::optional<double> FontPattern::pixelSize() const {
	if(!m_pattern)
		return {};

	double ret;
	auto res = FcPatternGetDouble(m_pattern, FC_PIXEL_SIZE, 0, &ret);
	if (res == FcResultMatch)
	       return ret;
	return {};
}

void FontPattern::setSlant(const Slant slant) {
	if(!m_pattern)
		return;

	FcPatternDel(m_pattern, FC_SLANT);
	FcPatternAddInteger(m_pattern, FC_SLANT, static_cast<int>(slant));
}

void FontPattern::setWeight(const Weight weight) {
	if(!m_pattern)
		return;

	FcPatternDel(m_pattern, FC_WEIGHT);
	FcPatternAddInteger(m_pattern, FC_WEIGHT, static_cast<int>(weight));
}

void FontPattern::destroy() {
	if (m_ext_pattern) {
		m_ext_pattern = false;
	} else if(m_pattern) {
		FcPatternDestroy(m_pattern);
	}

	m_pattern = nullptr;
}

void Font::unload() {
	if (m_match) {
		XftFontClose(xpp::display, m_match);
		m_match = nullptr;
	}
	if (m_pattern) {
		FcPatternDestroy(m_pattern);
		m_pattern = nullptr;
	}
	if (m_set) {
		FcFontSetDestroy(m_set);
		m_set = nullptr;
	}

	m_bad_slant = false;
	m_bad_weight = false;
}

bool Font::load(FontPattern &pattern) {
	unload();
	/*
	 * Manually configure instead of calling XftMatchFont
	 * so that we can use the configured pattern for
	 * "missing glyph" lookups.
	 */
	auto &display = xpp::display;
	FcPattern *configured = FcPatternDuplicate(pattern.raw());
	if (!configured) {
		return false;
	} else {
		FcPatternGuard configured_guard{configured};
		FcConfigSubstitute(nullptr, configured, FcMatchPattern);
		XftDefaultSubstitute(display, display.defaultScreen(), configured);

		FcResult result;
		FcPattern *m = FcFontMatch(nullptr, configured, &result);
		if (!m)
			return false;

		FcPatternGuard match_guard{m};

		if (!(m_match = XftFontOpenPattern(display, m)))
			return false;

		m_pattern = configured;

		// ownership is transferred now to pattern
		configured_guard.disarm();
		// ... and to m_match
		match_guard.disarm();
	}

	if (int wantattr; XftPatternGetInteger(pattern.raw(), "slant", 0, &wantattr) == XftResultMatch) {
		/*
		 * Check if xft was unable to find a font with the appropriate
		 * slant but gave us one anyway. Try to mitigate.
		 */
		if (int haveattr; (XftPatternGetInteger(m_match->pattern, "slant", 0, &haveattr) != XftResultMatch) ||
				haveattr < wantattr) {
			m_bad_slant = true;
			std::cerr << "font slant does not match\n";
		}
	}

	if (int wantattr; (XftPatternGetInteger(pattern.raw(), "weight", 0, &wantattr) == XftResultMatch)) {
		if (int haveattr; (XftPatternGetInteger(m_match->pattern, "weight", 0, &haveattr) != XftResultMatch) ||
				haveattr != wantattr) {
			m_bad_weight = true;
			std::cerr << "font weight does not match\n";
		}
	}

	XGlyphInfo extents;
	XftTextExtentsUtf8(display, m_match,
		(const FcChar8 *)config::ASCII_PRINTABLE.data(),
		config::ASCII_PRINTABLE.size(), &extents);

	m_ascent = m_match->ascent;
	m_descent = m_match->descent;

	m_height = m_ascent + m_descent;
	m_width = (extents.xOff + config::ASCII_PRINTABLE.size() - 1) / config::ASCII_PRINTABLE.size();

	return true;
}

FcPattern* Font::queryFontConfig(const Rune rune) {
	FcResult fc_res;
	if (!m_set) {
		m_set = FcFontSort(nullptr, m_pattern, /*trim=*/FcTrue, nullptr, &fc_res);
	}
	FcFontSet *fc_sets[] = { m_set };

	/*
	 * Nothing was found in the cache. Now use some dozen
	 * of Fontconfig calls to get the font for one single
	 * character.
	 *
	 * Xft and fontconfig are design failures.
	 */
	FcPattern *fc_pattern = FcPatternDuplicate(m_pattern);
	FcPatternGuard fc_pattern_guard{fc_pattern};
	FcCharSet *fc_charset = FcCharSetCreate();
	FcCharSetGuard fc_charset_guard{fc_charset};

	FcCharSetAddChar(fc_charset, rune);
	FcPatternAddCharSet(fc_pattern, FC_CHARSET, fc_charset);
	FcPatternAddBool(fc_pattern, FC_SCALABLE, FcTrue);

	FcConfigSubstitute(nullptr, fc_pattern, FcMatchPattern);
	FcDefaultSubstitute(fc_pattern);

	return FcFontSetMatch(nullptr, fc_sets, cosmos::num_elements(fc_sets), fc_pattern, &fc_res);
}

void FontManager::clearCache() {
	for (auto &entry: m_font_cache) {
		XftFontClose(xpp::display, entry.font);
	}

	m_font_cache.clear();
}

void FontManager::unloadFonts() {
	/* Free the loaded fonts in the font cache.  */
	clearCache();

	for (auto font: {&m_normal_font, &m_bold_font, &m_italics_font, &m_italics_bold_font}) {
		font->unload();
	}
}


FontManager::FontManager() {
	if (!FcInit()) {
		cosmos_throw (cosmos::RuntimeError("could not init fontconfig"));
	}
}


FontManager::~FontManager() {
	unloadFonts();
#if 0
	// TODO: enabling this fixed a leak but also causes an assertion on
	// shutdown, because something else within fontconfig was not
	// properly freed. Hard to hunt this down in the current state of font
	// handling code. Fix this together with font refactoring.
	FcFini();
#endif

}

void FontManager::zoom(double val) {
	*m_used_font_size += val;
	loadFonts();
}

void FontManager::resetZoom() {
	if (m_default_font_size) {
		m_used_font_size = m_default_font_size;
		loadFonts();
	}
}

bool FontManager::loadFonts() {
	FontPattern pattern{m_font_spec};

	if (!pattern.valid())
		return false;

	unloadFonts();

	if (m_used_font_size) {
		pattern.setPixelSize(*m_used_font_size);
	} else {
		if (auto pxsize = pattern.pixelSize(); pxsize.has_value())
			m_used_font_size = pxsize;
		else if(auto ptsize = pattern.pointSize(); ptsize.has_value())
			// leave to be determined after loading the first font below
			;
		else {
			/*
			 * Use default font size, if none given. This is to
			 * have a known m_used_font_size value.
			 */
			m_used_font_size = config::FONT_DEFAULT_SIZE_PX;
			pattern.setPixelSize(*m_used_font_size);
		}
		m_default_font_size = m_used_font_size;
	}

	if (!m_normal_font.load(pattern))
		return false;

	if (!m_used_font_size) {
		auto loaded = FontPattern{m_normal_font.match()->pattern};
		if (auto pxsize = loaded.pixelSize(); pxsize.has_value()) {
			m_used_font_size = *pxsize;
			if (!m_default_font_size)
				m_default_font_size = *pxsize;
		}
	}

	pattern.setSlant(Slant::ITALIC);
	if (!m_italics_font.load(pattern))
		return false;

	pattern.setWeight(Weight::BOLD);
	if (!m_italics_bold_font.load(pattern))
		return false;

	pattern.setSlant(Slant::ROMAN);
	if (!m_bold_font.load(pattern))
		return false;

	return true;
}

std::tuple<Font*, FontFlags> FontManager::fontForMode(const Glyph::AttrBitMask mode) {
	if (mode.allOf({Attr::ITALIC, Attr::BOLD})) {
		return std::make_tuple(&m_italics_bold_font, FontFlags::ITALIC_BOLD);
	} else if (mode[Attr::ITALIC]) {
		return std::make_tuple(&m_italics_font, FontFlags::ITALIC);
	} else if (mode[Attr::BOLD]) {
		return std::make_tuple(&m_bold_font, FontFlags::BOLD);
	} else {
		return std::make_tuple(&m_normal_font, FontFlags::NORMAL);
	}
}

std::tuple<XftFont*, FT_UInt> FontManager::lookupFontEntry(const Rune rune, Font &font, const FontFlags flags) {
	/* Lookup character index with default font. */
	auto glyphidx = XftCharIndex(xpp::display, font.match(), rune);
	if (glyphidx) {
		return std::make_tuple(font.match(), glyphidx);
	}

	/* Fallback on font cache, search the font cache for match. */
	for (auto &entry: m_font_cache) {
		glyphidx = XftCharIndex(xpp::display, entry.font, rune);
		if (glyphidx && entry.flags == flags) {
			/* Everything correct. */
			return std::make_tuple(entry.font, glyphidx);
		} else if (!glyphidx && entry.flags == flags && entry.rune == rune) {
			/* We got a default font for a not found glyph. */
			return std::make_tuple(entry.font, glyphidx);
		}
	}

	/* Nothing was found. Use fontconfig to find matching font. */
	auto pattern = font.queryFontConfig(rune);

	/* Allocate memory for the new cache entry. */
	auto new_font = XftFontOpenPattern(xpp::display, pattern);
	if (!new_font) {
		cosmos_throw (cosmos::ApiError("XftFontOpenPattern() failed seeking fallback font"));
	}

	m_font_cache.emplace_back(FontCache{new_font, flags, rune});

	glyphidx = XftCharIndex(xpp::display, new_font, rune);

	return std::make_tuple(new_font, glyphidx);
}

void FontManager::sanitizeColor(Glyph g) const {
	/* Fallback on color display for attributes not supported by the font */
	if (g.mode[Attr::ITALIC] && g.mode[Attr::BOLD]) {
		if (m_italics_bold_font.hasBadSlant() || m_italics_bold_font.hasBadWeight()) {
			g.fg = config::DEFAULT_ATTR;
		}
	} else if ((g.mode[Attr::ITALIC] && m_italics_font.hasBadSlant()) ||
			(g.mode[Attr::BOLD] && m_bold_font.hasBadWeight())) {
		g.fg = config::DEFAULT_ATTR;
	}
}


} // end ns
