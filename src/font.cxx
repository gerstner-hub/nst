// C++
#include <string>

// X11
#include <X11/Xft/Xft.h>

// nst
#include "font.hxx"

namespace nst {

bool FontPattern::parse(const std::string &str) {
	destroy();

	if (str[0] == '-')
		m_pattern = XftXlfdParse(str.c_str(), False, False);
	else
		m_pattern = FcNameParse((const FcChar8 *)str.c_str());

	return isValid();
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

void FontPattern::setSlant(const Slant &slant) {
	if(!m_pattern)
		return;

	FcPatternDel(m_pattern, FC_SLANT);
	FcPatternAddInteger(m_pattern, FC_SLANT, static_cast<int>(slant));
}

void FontPattern::setWeight(const Weight &weight) {
	if(!m_pattern)
		return;

	FcPatternDel(m_pattern, FC_WEIGHT);
	FcPatternAddInteger(m_pattern, FC_WEIGHT, static_cast<int>(weight));
}

void FontPattern::destroy() {
	if (!m_pattern)
		return;
	else if(m_ext_pattern) {
		m_pattern = nullptr;
		m_ext_pattern = false;
		return;
	}

	FcPatternDestroy(m_pattern);
	m_pattern = nullptr;
}

void Font::reset(xpp::XDisplay &d) {
	if (match) {
		XftFontClose(d, match);
		match = nullptr;
	}
	if (pattern) {
		FcPatternDestroy(pattern);
		pattern = nullptr;
	}
	if (set) {
		FcFontSetDestroy(set);
		set = nullptr;
	}

	badslant = false;
	badweight = false;
}

} // end ns
