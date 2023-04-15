// C++
#include <iostream>

// cosmos
#include "cosmos/algs.hxx"
#include "cosmos/error/ApiError.hxx"
#include "cosmos/error/RuntimeError.hxx"
#include "cosmos/types.hxx"

// X++
#include "X++/helpers.hxx"
#include "X++/Pixmap.hxx"
#include "X++/XDisplay.hxx"

// nst
#include "color.hxx"
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
		m_pattern = ::XftXlfdParse(spec.data(), False, False);
	else
		m_pattern = FcNameParse((const FcChar8*)spec.data());

	return valid();
}

void FontPattern::setPixelSize(double size_px) {
	FcPatternDel(m_pattern, FC_PIXEL_SIZE);
	FcPatternDel(m_pattern, FC_SIZE);
	FcPatternAddDouble(m_pattern, FC_PIXEL_SIZE, size_px);
}

std::optional<double> FontPattern::pointSize() const {
	return getDouble(FC_SIZE);
}

std::optional<double> FontPattern::pixelSize() const {
	return getDouble(FC_PIXEL_SIZE);
}

std::optional<double> FontPattern::getDouble(const std::string_view which) const {
	if(!m_pattern)
		return {};

	double ret;
	auto res = FcPatternGetDouble(m_pattern, which.data(), 0, &ret);
	if (res == FcResultMatch)
		return ret;
	else
		return {};
}

void FontPattern::setSlant(const Slant slant) {
	if(!m_pattern)
		return;

	FcPatternDel(m_pattern, FC_SLANT);
	FcPatternAddInteger(m_pattern, FC_SLANT, cosmos::to_integral(slant));
}

std::optional<Slant> FontPattern::getSlant() const {
	auto ret = getInt(FC_SLANT);

	if (!ret)
		return {};

	return Slant{*ret};
}

void FontPattern::setWeight(const Weight weight) {
	if(!m_pattern)
		return;

	FcPatternDel(m_pattern, FC_WEIGHT);
	FcPatternAddInteger(m_pattern, FC_WEIGHT, cosmos::to_integral(weight));
}

std::optional<Weight> FontPattern::getWeight() const {
	auto ret = getInt(FC_WEIGHT);

	if (!ret)
		return {};

	return Weight{*ret};
}

std::optional<int> FontPattern::getInt(const std::string_view which) const {
	int attr;
	auto res = ::XftPatternGetInteger(m_pattern, which.data(), 0, &attr);

	if (res != XftResultMatch) {
		return {};
	}

	return attr;
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
		::XftFontClose(xpp::display, m_match);
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

bool Font::load(const FontPattern &pattern) {
	unload();
	// Manually configure instead of calling XftMatchFont so that we can
	// use the configured pattern for "missing glyph" lookups.
	auto &display = xpp::display;
	FcPattern *configured = FcPatternDuplicate(pattern.raw());
	if (!configured) {
		return false;
	} else {
		FcPatternGuard configured_guard{configured};
		FcConfigSubstitute(nullptr, configured, FcMatchPattern);
		::XftDefaultSubstitute(display, xpp::raw_screen(display.defaultScreen()), configured);

		FcResult result;
		FcPattern *match = FcFontMatch(nullptr, configured, &result);
		if (!match)
			return false;

		FcPatternGuard match_guard{match};

		if (!(m_match = ::XftFontOpenPattern(display, match)))
			return false;

		m_pattern = configured;

		// ownership has been transferred to m_pattern
		configured_guard.disarm();
		// ... and to m_match
		match_guard.disarm();
	}

	checkSlant(pattern);
	checkWeight(pattern);

	XGlyphInfo extents;
	::XftTextExtentsUtf8(display, m_match,
		(const FcChar8*)config::ASCII_PRINTABLE.data(),
		config::ASCII_PRINTABLE.size(), &extents);

	// pixels above and below the baseline of a character make up the
	// character height
	m_height = ascent() + descent();
	m_width = (extents.xOff + config::ASCII_PRINTABLE.size() - 1) / config::ASCII_PRINTABLE.size();

	return true;
}

void Font::checkSlant(const FontPattern &pattern) {
	const FontPattern opened = this->pattern();

	if (auto wanted_slant = pattern.getSlant(); wanted_slant != std::nullopt) {
		// Check if xft was unable to find a font with the appropriate
		// slant but gave us one anyway. Try to mitigate.
		if (auto our_slant = opened.getSlant(); our_slant != std::nullopt && *our_slant < *wanted_slant) {
			m_bad_slant = true;
			std::cerr << "font slant does not match\n";
		}
	}
}

void Font::checkWeight(const FontPattern &pattern) {
	const FontPattern opened = this->pattern();

	if (auto wanted_weight = pattern.getWeight(); wanted_weight != std::nullopt) {
		if (auto our_weight = opened.getWeight(); our_weight != std::nullopt && *our_weight != *wanted_weight) {
			m_bad_weight = true;
			std::cerr << "font weight does not match\n";
		}
	}
}

FcPattern* Font::queryFontConfig(const Rune rune) const {
	FcResult fc_res;
	if (!m_set) {
		// TODO: error checking?
		m_set = FcFontSort(nullptr, m_pattern, /*trim=*/FcTrue, nullptr, &fc_res);
	}
	FcFontSet *fc_sets[] = { m_set };

	// Nothing was found in the cache. Now use some dozen of Fontconfig
	// calls to get the font for one single character.
	//
	// Xft and fontconfig are design failures.
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
		::XftFontClose(xpp::display, entry.font);
	}

	m_font_cache.clear();
}

void FontManager::unloadFonts() {
	// Free the loaded fonts in the font cache.
	clearCache();

	for (auto font: {&m_normal_font, &m_bold_font, &m_italic_font, &m_italic_bold_font}) {
		font->unload();
	}
}

FontManager::FontManager() :
		m_normal_font{FontFlags::NORMAL},
		m_bold_font{FontFlags::BOLD},
		m_italic_font{FontFlags::ITALIC},
		m_italic_bold_font{FontFlags::ITALIC_BOLD} {
	if (!FcInit()) {
		cosmos_throw (cosmos::RuntimeError("could not init fontconfig"));
	}
}


FontManager::~FontManager() {
	unloadFonts();
#if 0
	/*
	 * Calling this results in an assertion on shutdown, because some
	 * caches are not freed within fontconfig. After investigating this
	 * more closely it seems this is not due to a leak caused by NST, but
	 * due to libXft's handling of fontconfig. There is XftInit() that
	 * also calls FcInit(), but there is no counterpart in libXft to
	 * cleanup ... it seems like the leaks stem from libXft and there is
	 * nothing we can do against that.
	 */
	FcFini();
#endif
}

void FontManager::zoom(const double val) {
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
			// Use default font size, if none given. This is to
			// have a known m_used_font_size value.
			m_used_font_size = config::FONT_DEFAULT_SIZE_PX;
			pattern.setPixelSize(*m_used_font_size);
		}
		m_default_font_size = m_used_font_size;
	}

	if (!m_normal_font.load(pattern))
		return false;

	if (!m_used_font_size) {
		auto loaded = m_normal_font.pattern();
		if (auto pxsize = loaded.pixelSize(); pxsize.has_value()) {
			m_used_font_size = *pxsize;
			if (!m_default_font_size)
				m_default_font_size = *pxsize;
		}
	}

	pattern.setSlant(Slant::ITALIC);
	if (!m_italic_font.load(pattern))
		return false;

	pattern.setWeight(Weight::BOLD);
	if (!m_italic_bold_font.load(pattern))
		return false;

	pattern.setSlant(Slant::ROMAN);
	if (!m_bold_font.load(pattern))
		return false;

	return true;
}

Font* FontManager::fontForMode(const Glyph::AttrBitMask mode) {
	if (mode.allOf({Attr::ITALIC, Attr::BOLD})) {
		return &m_italic_bold_font;
	} else if (mode[Attr::ITALIC]) {
		return &m_italic_font;
	} else if (mode[Attr::BOLD]) {
		return &m_bold_font;
	} else {
		return &m_normal_font;
	}
}

void FontManager::assignFont(const Rune rune, Font &font, GlyphFontSpec &spec) {
	auto [xftfont, glyphidx] = lookupFontEntry(rune, font);
	spec.font = xftfont;
	spec.glyph = glyphidx;
}

// font cannot be const since libXft doesn't mark it const in XftCharIndex,
// also the implementation actually changes data in it (caches)
std::tuple<XftFont*, FT_UInt> FontManager::lookupFontEntry(const Rune rune, Font &font) {
	// Lookup character index with default font.
	auto glyphidx = ::XftCharIndex(xpp::display, font.match(), rune);
	if (glyphidx) {
		return std::make_tuple(font.match(), glyphidx);
	}

	// Fallback on font cache, search the font cache for match.
	for (auto &entry: m_font_cache) {
		glyphidx = ::XftCharIndex(xpp::display, entry.font, rune);
		if (glyphidx && entry.flags == font.flags()) {
			// Everything correct.
			return std::make_tuple(entry.font, glyphidx);
		} else if (!glyphidx && entry.flags == font.flags() && entry.rune == rune) {
			// We got a default font for a not found glyph.
			return std::make_tuple(entry.font, glyphidx);
		}
	}

	// Nothing was found. Use fontconfig to find matching font.
	auto pattern = font.queryFontConfig(rune);

	// Allocate memory for the new cache entry.
	auto new_font = ::XftFontOpenPattern(xpp::display, pattern);
	if (!new_font) {
		cosmos_throw (cosmos::ApiError("XftFontOpenPattern() failed seeking fallback font"));
	}

	m_font_cache.emplace_back(FontCache{new_font, font.flags(), rune});

	glyphidx = ::XftCharIndex(xpp::display, new_font, rune);

	return std::make_tuple(new_font, glyphidx);
}

void FontManager::sanitize(Glyph &g) const {
	// Fallback on color display for attributes not supported by the font
	if (g.mode[Attr::ITALIC] && g.mode[Attr::BOLD]) {
		if (m_italic_bold_font.hasBadSlant() || m_italic_bold_font.hasBadWeight()) {
			g.fg = config::DEFAULT_ATTR;
		}
	} else if ((g.mode[Attr::ITALIC] && m_italic_font.hasBadSlant()) ||
			(g.mode[Attr::BOLD] && m_bold_font.hasBadWeight())) {
		g.fg = config::DEFAULT_ATTR;
	}
}

void FontDrawContext::destroy() {
	if (m_ctx) {
		::XftDrawDestroy(m_ctx);
		m_ctx = nullptr;
	}
}

void FontDrawContext::setup(xpp::XDisplay &disp, xpp::Pixmap &pixmap) {
	if (m_ctx) {
		::XftDrawChange(m_ctx, xpp::raw_pixmap(pixmap));
	} else {
		m_ctx = ::XftDrawCreate(disp, xpp::raw_pixmap(pixmap), xpp::visual, xpp::raw_cmap(xpp::colormap));
	}
}

void FontDrawContext::drawRect(const FontColor &color, const DrawPos start, const Extent ext) {
	::XftDrawRect(m_ctx, &color, start.x, start.y, ext.width, ext.height);
}

void FontDrawContext::drawSpecs(
		const FontColor &color, GlyphFontSpecVector::iterator start, GlyphFontSpecVector::iterator end) {
	::XftDrawGlyphFontSpec(m_ctx, &color, &(*start), end - start);
}

void FontDrawContext::setClipRectangle(const DrawPos pos, const Extent ext) {
	XRectangle r{0, 0, static_cast<unsigned short>(ext.width), static_cast<unsigned short>(ext.height)};
	::XftDrawSetClipRectangles(m_ctx, pos.x, pos.y, &r, /*n_rects=*/1);
}

void FontDrawContext::resetClip() {
	::XftDrawSetClip(m_ctx, 0);
}

} // end ns
