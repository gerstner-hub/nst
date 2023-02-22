// libc
#include <limits.h>
#include <locale.h>
#include <unistd.h>

// libX11 et al
#include <X11/Xatom.h>
#include <X11/Xlib.h>
#include <X11/keysym.h>
#include <X11/XKBlib.h>

// stdlib
#include <algorithm>
#include <iostream>

// cosmos
#include "cosmos/algs.hxx"
#include "cosmos/types.hxx"
#include "cosmos/error/ApiError.hxx"
#include "cosmos/error/RuntimeError.hxx"
#include "cosmos/formatting.hxx"
#include "cosmos/proc/Process.hxx"

// X++
#include "X++/helpers.hxx"
#include "X++/Event.hxx"
#include "X++/RootWin.hxx"
#include "X++/XDisplay.hxx"
#include "X++/Xpp.hxx"

// nst
#include "codecs.hxx"
/* nst_config.h for applying patches and the configuration. */
#include "nst_config.hxx"
#include "nst.hxx"
#include "Selection.hxx"
#include "Term.hxx"
#include "TTY.hxx"
#include "types.hxx"
#include "x11.hxx"
#include "XSelection.hxx"

namespace {

template <typename T, typename V>
inline void modifyBit(T &mask, const bool set, const V &bit) {
	if (set)
		mask |= bit;
	else
		mask &= ~bit;
}

} // end anon ns

namespace nst {

void DrawingContext::createGC(xpp::XDisplay &display, xpp::XWindow &parent) {
	m_display = &display;
	XGCValues gcvalues = {};
	gcvalues.graphics_exposures = False;
	m_gc = display.createGraphicsContext(
			parent,
			xpp::GcOptMask(xpp::GcOpts::GraphicsExposures),
			gcvalues);
}

std::tuple<Font*, FontFlags> DrawingContext::getFontForMode(const Glyph::AttrBitMask &mode) {
	if (mode.allOf({Attr::ITALIC, Attr::BOLD})) {
		return std::make_tuple(&ibfont, FontFlags::ITALICBOLD);
	} else if (mode[Attr::ITALIC]) {
		return std::make_tuple(&ifont, FontFlags::ITALIC);
	} else if (mode[Attr::BOLD]) {
		return std::make_tuple(&bfont, FontFlags::BOLD);
	} else {
		return std::make_tuple(&font, FontFlags::NORMAL);
	}
}

void DrawingContext::setForeground(const FontColor &color) {
	XSetForeground(*m_display, getRawGC(), color.pixel);
}

void DrawingContext::fillRectangle(const DrawPos &pos, const Extent &ext) {
	XFillRectangle(*m_display, m_pixmap.id(), getRawGC(), pos.x, pos.y, ext.width, ext.height);
}

void DrawingContext::sanitizeColor(Glyph &g) const {
	/* Fallback on color display for attributes not supported by the font */
	if (g.mode[Attr::ITALIC] && g.mode[Attr::BOLD]) {
		if (ibfont.badslant || ibfont.badweight) {
			g.fg = config::DEFAULTATTR;
		}
	} else if ((g.mode[Attr::ITALIC] && ifont.badslant) || (g.mode[Attr::BOLD] && bfont.badweight)) {
		g.fg = config::DEFAULTATTR;
	}
}

void RenderColor::setFromRGB(const Glyph::color_t rgb) {
	/* seems like the X color values are 16-bit wide and we need to
	 * translate the one color bytes into the upper byte in the
	 * XRenderColor */
	alpha = 0xffff;
	red = (rgb & 0xff0000) >> 8;
	green = (rgb & 0xff00);
	blue = (rgb & 0xff) << 8;
}

X11::X11(Nst &nst) : m_nst(nst), m_input(*this), m_xsel(nst), m_tsize{config::COLS, config::ROWS} {
	m_tsize.normalize();
	setCursorStyle(config::CURSORSHAPE);
}

X11::~X11() {
	if (m_font_draw) {
		XftDrawDestroy(m_font_draw);
	}
	m_draw_ctx.freeGC();
	unloadFonts();
#if 0
	// TODO: enabling this fixed a leak but also causes an assertion on
	// shutdown, because something else withing fontconfig was not
	// properly freed. Hard to hunt this down in the current state of font
	// handling code. Fix this together with font refactoring.
	if (m_fc_inited)
		FcFini();
#endif
}

void X11::copyToClipboard() {
	m_xsel.copyPrimaryToClipboard();
}

void X11::pasteClipboard() {
	auto clipboard = getXAtom("CLIPBOARD");
	m_window.convertSelection(clipboard, m_xsel.getTargetFormat(), clipboard);
}

void X11::pasteSelection() {
	auto primary = xpp::XAtom(XA_PRIMARY);
	m_window.convertSelection(primary, m_xsel.getTargetFormat(), primary);
}

void X11::toggleNumlock() {
	m_twin.flipFlag(WinMode::NUMLOCK);
}

void X11::zoomFont(float val) {
	unloadFonts();
	loadFontsOrThrow(m_cmdline->font.getValue(), m_used_font_size + (double)val);
	m_nst.resizeConsole();
	m_nst.getTerm().redraw();
	setHints();
}

void X11::resetFont() {
	if (m_default_font_size > 0) {
		m_used_font_size = m_default_font_size;
		zoomFont(0);
	}
}

void X11::allocPixmap() {
	if (m_pixmap.valid()) {
		m_display->freePixmap(m_pixmap);
	}
	m_pixmap = m_display->createPixmap(
		m_window,
		m_twin.getWinExtent()
	);

	if (m_font_draw) {
		XftDrawChange(m_font_draw, m_pixmap.id());
	} else {
		/* Xft rendering context */
		m_font_draw = XftDrawCreate(*m_display, m_pixmap.id(), m_visual, m_color_map);
	}

	m_draw_ctx.setPixmap(m_pixmap);
}

void X11::resize(const TermSize &dim) {

	m_twin.setTermDim(dim);
	allocPixmap();
	const auto &win = m_twin.getWinExtent();
	clearRect(DrawPos{0,0}, DrawPos{win.width, win.height});

	/* resize to new width */
	m_font_specs.resize(dim.cols);
}

int X11::loadColor(size_t i, const char *name, FontColor *ncolor) {
	XRenderColor color = { 0, 0, 0, 0xfff };

	auto sixd_to_16bit = [](size_t x) -> uint16_t {
		return x == 0 ? 0 : 0x3737 + 0x2828 * x;
	};

	if (!name) {
		if (cosmos::in_range(i, 16, 255)) { /* 256 color */
			if (i < 6*6*6+16) { /* same colors as xterm */
				color.red   = sixd_to_16bit( ((i-16)/36)%6 );
				color.green = sixd_to_16bit( ((i-16)/6) %6 );
				color.blue  = sixd_to_16bit( ((i-16)/1) %6 );
			} else { /* greyscale */
				color.red = 0x0808 + 0x0a0a * (i - (6*6*6+16));
				color.green = color.blue = color.red;
			}
			return XftColorAllocValue(*m_display, m_visual,
			                          m_color_map, &color, ncolor);
		} else {
			auto cname = config::getColorName(i);
			name = cname.empty() ? nullptr : cname.data();
		}
	}

	return XftColorAllocName(*m_display, m_visual, m_color_map, name, ncolor);
}

void X11::loadColors() {
	if (m_colors_loaded) {
		for (auto &c: m_draw_ctx.col) {
			XftColorFree(*m_display, m_visual, m_color_map, &c);
		}
	} else {
		m_color_map = m_display->getDefaultColormap(m_screen);
		auto len = std::max(256UL + config::EXTENDED_COLORS.size(), 256UL);
		m_draw_ctx.col.resize(len);
	}

	for (size_t i = 0; i < m_draw_ctx.col.size(); i++) {
		if (!loadColor(i, nullptr, &m_draw_ctx.col[i])) {
			if (auto colorname = config::getColorName(i); !colorname.empty())
				cosmos_throw (cosmos::ApiError(cosmos::sprintf("could not allocate color '%s'", colorname.data())));
			else
				cosmos_throw (cosmos::ApiError(cosmos::sprintf("could not allocate color %zd", i)));
		}
	}

	m_colors_loaded = true;
}

bool X11::getColor(size_t idx, unsigned char *r, unsigned char *g, unsigned char *b) const {
	if (idx >= m_draw_ctx.col.size())
		return false;

	*r = m_draw_ctx.col[idx].color.red >> 8;
	*g = m_draw_ctx.col[idx].color.green >> 8;
	*b = m_draw_ctx.col[idx].color.blue >> 8;

	return true;
}

bool X11::setColorName(size_t idx, const char *name) {
	if (idx >= m_draw_ctx.col.size())
		return false;

	FontColor ncolor;
	if (!loadColor(idx, name, &ncolor))
		return false;

	XftColorFree(*m_display, m_visual, m_color_map, &m_draw_ctx.col[idx]);
	m_draw_ctx.col[idx] = ncolor;

	return true;
}

void X11::clearRect(const DrawPos &pos1, const DrawPos &pos2) {
	const auto colindex = m_twin.getActiveForegroundColor();
	drawRect(m_draw_ctx.col[colindex], pos1, Extent{pos2.x - pos1.x, pos2.y - pos1.y});
}

void X11::drawRect(const FontColor &col, const DrawPos &start, const Extent &ext) {
	XftDrawRect(m_font_draw, &col, start.x, start.y, ext.width, ext.height);
}

void X11::setHints() {
	// note: the X API breaks constness here, thus use a by-value-copy of
	// the command line arguments
	auto wname = m_cmdline->window_name.getValue();
	auto wclass = m_cmdline->window_class.getValue();
	const auto &chr = m_twin.getChrExtent();
	const auto &win = m_twin.getWinExtent();
	XClassHint clazz = {&wname[0], &wclass[0]};
	XWMHints wm = {InputHint, 1, 0, 0, 0, 0, 0, 0, 0};
	auto sizeh = xpp::make_shared_xptr(XAllocSizeHints());

	sizeh->flags = PSize | PResizeInc | PBaseSize | PMinSize;
	sizeh->height = win.height;
	sizeh->width = win.width;
	sizeh->height_inc = chr.height;
	sizeh->width_inc = chr.width;
	sizeh->base_height = 2 * config::BORDERPX;
	sizeh->base_width = 2 * config::BORDERPX;
	sizeh->min_height = chr.height + 2 * config::BORDERPX;
	sizeh->min_width = chr.width + 2 * config::BORDERPX;
	if (m_fixed_geometry) {
		sizeh->flags |= PMaxSize;
		sizeh->min_width = sizeh->max_width = win.width;
		sizeh->min_height = sizeh->max_height = win.height;
	}
	if (m_geometry & (XValue|YValue)) {
		sizeh->flags |= USPosition | PWinGravity;
		sizeh->x = m_win_offset.x;
		sizeh->y = m_win_offset.y;
		sizeh->win_gravity = getGravity();
	}

	XSetWMProperties(*m_display, m_window, NULL, NULL, NULL, 0, sizeh.get(), &wm, &clazz);
}

int X11::getGravity() {
	switch (m_geometry & (XNegative|YNegative)) {
	case 0:
		return NorthWestGravity;
	case XNegative:
		return NorthEastGravity;
	case YNegative:
		return SouthWestGravity;
	default:
		return SouthEastGravity;
	}
}

int X11::loadFont(Font *f, FcPattern *pattern) {
	/*
	 * Manually configure instead of calling XftMatchFont
	 * so that we can use the configured pattern for
	 * "missing glyph" lookups.
	 */
	FcPattern *configured = FcPatternDuplicate(pattern);
	if (!configured)
		return 1;

	FcPatternGuard configured_guard(configured);
	FcConfigSubstitute(nullptr, configured, FcMatchPattern);
	XftDefaultSubstitute(*m_display, m_screen, configured);

	FcResult result;
	FcPattern *match = FcFontMatch(nullptr, configured, &result);
	if (!match)
		return 1;

	FcPatternGuard match_guard(match);

	if (!(f->match = XftFontOpenPattern(*m_display, match)))
		return 1;

	// ownership will be transferred now
	configured_guard.disarm();
	// TODO: memory leak? Is this match pattern really transferred?
	match_guard.disarm();

	if (int wantattr; (XftPatternGetInteger(pattern, "slant", 0, &wantattr) == XftResultMatch)) {
		/*
		 * Check if xft was unable to find a font with the appropriate
		 * slant but gave us one anyway. Try to mitigate.
		 */
		if (int haveattr; (XftPatternGetInteger(f->match->pattern, "slant", 0,
		    &haveattr) != XftResultMatch) || haveattr < wantattr) {
			f->badslant = 1;
			fputs("font slant does not match\n", stderr);
		}
	}

	if (int wantattr; (XftPatternGetInteger(pattern, "weight", 0, &wantattr) == XftResultMatch)) {
		if (int haveattr; (XftPatternGetInteger(f->match->pattern, "weight", 0,
		    &haveattr) != XftResultMatch) || haveattr != wantattr) {
			f->badweight = 1;
			fputs("font weight does not match\n", stderr);
		}
	}

	XGlyphInfo extents;
	XftTextExtentsUtf8(*m_display, f->match, (const FcChar8 *) config::ASCII_PRINTABLE,
		config::ASCII_PRINTABLE_LEN, &extents);

	f->set = nullptr;
	f->pattern = configured;

	f->ascent = f->match->ascent;
	f->descent = f->match->descent;
	f->lbearing = 0;
	f->rbearing = f->match->max_advance_width;

	f->height = f->ascent + f->descent;
	f->width = (extents.xOff + config::ASCII_PRINTABLE_LEN - 1) / config::ASCII_PRINTABLE_LEN;

	return 0;
}

bool X11::loadFonts(const std::string &fontstr, double fontsize) {
	FontPattern pattern(fontstr);

	if (!pattern.isValid())
		return false;

	if (fontsize > 1) {
		pattern.setPixelSize(fontsize);
		m_used_font_size = fontsize;
	} else {
		if (auto pxsize = pattern.getPixelSize(); pxsize.has_value())
			m_used_font_size = *pxsize;
		else if(auto ptsize = pattern.getPointSize(); ptsize.has_value())
			m_used_font_size = -1;
		else {
			/*
			 * Use default font size, if none given. This is to
			 * have a known m_used_font_size value.
			 */
			m_used_font_size = config::FONT_DEFAULT_SIZE_PX;
			pattern.setPixelSize(m_used_font_size);
		}
		m_default_font_size = m_used_font_size;
	}

	if (loadFont(&m_draw_ctx.font, pattern.raw()))
		return false;

	if (m_used_font_size < 0) {
		auto loaded = FontPattern(m_draw_ctx.font.match->pattern);
		if (auto pxsize = loaded.getPixelSize(); pxsize.has_value()) {
			m_used_font_size = *pxsize;
			if (fontsize == 0)
				m_default_font_size = *pxsize;
		}
	}

	/* Setting character width and height. */
	m_twin.setCharSize(m_draw_ctx.font);

	pattern.setSlant(Slant::ITALIC);
	if (loadFont(&m_draw_ctx.ifont, pattern.raw()))
		return false;

	pattern.setWeight(Weight::BOLD);
	if (loadFont(&m_draw_ctx.ibfont, pattern.raw()))
		return false;

	pattern.setSlant(Slant::ROMAN);
	if (loadFont(&m_draw_ctx.bfont, pattern.raw()))
		return false;

	return true;
}

void X11::loadFontsOrThrow(const std::string &fontstr, double fontsize) {
	if (!loadFonts(fontstr, fontsize)) {
		cosmos_throw (cosmos::RuntimeError(cosmos::sprintf("failed to open font %s", fontstr.c_str())));
	}
}

void X11::unloadFont(Font *f) {
	if (f->match) {
		XftFontClose(*m_display, f->match);
		f->match = nullptr;
	}
	if (f->pattern) {
		FcPatternDestroy(f->pattern);
		f->pattern = nullptr;
	}
	if (f->set) {
		FcFontSetDestroy(f->set);
		f->set = nullptr;
	}
}

void X11::unloadFonts() {
	/* Free the loaded fonts in the font cache.  */
	for (auto &fc: m_font_cache)
		XftFontClose(*m_display, fc.font);

	m_font_cache.clear();

	for (auto font: {&m_draw_ctx.font, &m_draw_ctx.bfont, &m_draw_ctx.ifont, &m_draw_ctx.ibfont}) {
		unloadFont(font);
	}
}

bool X11::ximOpen() {
	if (m_input.open())
		return true;

	m_input.installCallback();
	return false;
}

X11::Input::~Input() {
	if (m_spotlist)
		XFree(m_spotlist);
}

void X11::Input::installCallback() {
	XRegisterIMInstantiateCallback(m_x11.getDisplay(), nullptr, nullptr, nullptr,
				       &Input::instMethodCB, (XPointer)this);
}

bool X11::Input::open() {
	XIMCallback imdestroy = { .client_data = (XPointer)this, .callback = destroyMethodCB };
	XICCallback icdestroy = { .client_data = (XPointer)this, .callback = destroyContextCB };

	m_method = XOpenIM(m_x11.getDisplay(), nullptr, nullptr, nullptr);
	if (!m_method)
		return false;

	if (XSetIMValues(m_method, XNDestroyCallback, &imdestroy, nullptr))
		fprintf(stderr, "XSetIMValues: "
		                "Could not set XNDestroyCallback.\n");

	m_spotlist = XVaCreateNestedList(0, XNSpotLocation, &m_spot, nullptr);

	if (!m_ctx) {
		// NOTE: this function takes varargs, passing in C++ objects
		// like m_window even with conversion operator does not work
		m_ctx = XCreateIC(m_method,
		                       XNInputStyle,
		                       XIMPreeditNothing | XIMStatusNothing,
		                       XNClientWindow, m_x11.getWindow().id(),
		                       XNDestroyCallback, &icdestroy,
		                       nullptr);
	}

	if (!m_ctx) {
		fprintf(stderr, "XCreateIC: Could not create input context.\n");
	}

	return true;
}

void X11::Input::instMethod() {
	if (!open())
		return;

	XUnregisterIMInstantiateCallback(m_x11.getDisplay(), nullptr, nullptr, nullptr,
					 &instMethodCB, (XPointer)this);
}

void X11::Input::instMethodCB(Display *, XPointer inputp, XPointer) {
	auto &input = *reinterpret_cast<Input*>(inputp);
	input.instMethod();
}

void X11::Input::destroyMethod() {
	m_method = nullptr;
	installCallback();
	XFree(m_spotlist);
	m_spotlist = nullptr;
}

void X11::Input::destroyMethodCB(XIM, XPointer inputp, XPointer) {
	auto &input = *reinterpret_cast<Input*>(inputp);
	input.destroyMethod();
}

int X11::Input::destroyContext() {
	m_ctx = nullptr;
	return 1;
}

int X11::Input::destroyContextCB(XIC, XPointer inputp, XPointer) {
	auto &input = *reinterpret_cast<Input*>(inputp);
	return input.destroyContext();
}

void X11::Input::setSpot(const CharPos &chp) {
	if (!m_ctx)
		return;

	const auto dp = m_x11.getTermWin().getDrawPos(chp.nextLine());

	m_spot.x = dp.x;
	m_spot.y = dp.y;

	XSetICValues(m_ctx, XNPreeditAttributes, m_spotlist, nullptr);
}

void X11::Input::setFocus() {
	if (!haveContext())
		return;

	XSetICFocus(m_ctx);
}

void X11::Input::unsetFocus() {
	if (!haveContext())
		return;

	XUnsetICFocus(m_ctx);
}

KeySym X11::Input::lookupString(const XKeyEvent &ev, std::string &s) {
	int len;
	KeySym sym;

	s.resize(64);

	if (haveContext()) {
		Status status;
		len = XmbLookupString(
				getContext(), const_cast<XKeyEvent*>(&ev),
				&s[0], s.size() + 1,
				&sym, &status);
	}
	else {
		len = XLookupString(const_cast<XKeyEvent*>(&ev), &s[0], s.size() + 1, &sym, nullptr);
	}

	s.resize(len);
	return sym;
}

void X11::setGeometry(const std::string &g) {
	unsigned int cols, rows;

	m_geometry = XParseGeometry(g.c_str(), &m_win_offset.x, &m_win_offset.y, &cols, &rows);

	m_tsize.rows = rows;
	m_tsize.cols = cols;
	m_twin.setWinExtent(m_tsize);
	const auto &win = m_twin.getWinExtent();
	if (m_geometry & XNegative)
		m_win_offset.x += m_display->getDisplayWidth(m_screen) - win.width - 2;
	if (m_geometry & YNegative)
		m_win_offset.y  += m_display->getDisplayHeight(m_screen) - win.height - 2;
}

xpp::XWindow X11::getParent() const {
	xpp::XWindow ret;

	if (m_cmdline->embed_window.isSet()) {
		// use window ID passed on command line as parent
		ret = xpp::XWindow(m_cmdline->embed_window.getValue());
	}

	if (!ret.valid()) {
		// either not embedded or the command line parsing failed
		ret = xpp::RootWin(*m_display, m_screen);
	}

	return ret;
}

void X11::setupCursor() {
	/* white cursor, black outline */
	Cursor cursor = XCreateFontCursor(*m_display, config::MOUSESHAPE);
	XDefineCursor(*m_display, m_window, cursor);

	XColor xmousefg, xmousebg;

	auto parseColor = [this](size_t colnr, XColor &out) {
		auto cname = config::getColorName(colnr);
		auto res = XParseColor(*m_display, m_color_map, cname.empty() ? nullptr : cname.data(), &out);
		return res != 0;
	};

	if (!parseColor(config::MOUSEFG, xmousefg)) {
		xmousefg.red   = 0xffff;
		xmousefg.green = 0xffff;
		xmousefg.blue  = 0xffff;
	}

	if (!parseColor(config::MOUSEBG, xmousebg)) {
		xmousebg.red   = 0x0000;
		xmousebg.green = 0x0000;
		xmousebg.blue  = 0x0000;
	}

	XRecolorCursor(*m_display, cursor, &xmousefg, &xmousebg);
}

void X11::init() {
	m_cmdline = &m_nst.getCmdline();
	m_display = &xpp::XDisplay::getInstance();
	m_mapper = &xpp::XAtomMapper::getInstance();
	m_screen = m_display->getDefaultScreen();
	m_visual = m_display->getDefaultVisual(m_screen);

	m_fixed_geometry = m_cmdline->fixed_geometry.isSet();

	/* font */
	if (!FcInit())
		cosmos_throw (cosmos::RuntimeError("could not init fontconfig"));

	m_fc_inited = true;

	loadFontsOrThrow(m_cmdline->font.getValue());

	/* colors */
	loadColors();

	/* adjust fixed window geometry */
	if (m_cmdline->window_geometry.isSet()) {
		setGeometry(m_cmdline->window_geometry.getValue());
	}

	m_twin.setWinExtent(m_tsize);
	/* font spec buffer */
	m_font_specs.resize(m_tsize.cols);

	/* Events */
	const auto &bgcolor = m_draw_ctx.col[config::DEFAULTBG];
	m_win_attrs.background_pixel = bgcolor.pixel;
	m_win_attrs.border_pixel = bgcolor.pixel;
	m_win_attrs.bit_gravity = NorthWestGravity;
	m_win_attrs.event_mask = FocusChangeMask | KeyPressMask | KeyReleaseMask
		| ExposureMask | VisibilityChangeMask | StructureNotifyMask
		| ButtonMotionMask | ButtonPressMask | ButtonReleaseMask;
	m_win_attrs.colormap = m_color_map;

	auto parent = getParent();
	const auto &win = m_twin.getWinExtent();

	m_window = m_display->createWindow(
		xpp::WindowSpec{m_win_offset.x, m_win_offset.y,
			static_cast<unsigned int>(win.width),
			static_cast<unsigned int>(win.height)},
		/*border_width=*/0,
		/*clazz = */InputOutput,
		&parent,
		m_display->getDefaultDepth(m_screen),
		m_visual,
		CWBackPixel | CWBorderPixel | CWBitGravity | CWEventMask | CWColormap,
		&m_win_attrs
	);

	m_draw_ctx.createGC(*m_display, parent);
	allocPixmap();
	m_draw_ctx.setForeground(bgcolor);
	m_draw_ctx.fillRectangle(DrawPos{0,0}, win);

	/* input methods */
	ximOpen();

	setupCursor();

	m_wmdeletewin = getXAtom("WM_DELETE_WINDOW");
	m_netwmname = getXAtom("_NET_WM_NAME");
	m_wmname = getXAtom("WM_NAME");
	m_netwmiconname = getXAtom("_NET_WM_ICON_NAME");

	m_window.setProtocols(xpp::XAtomVector{m_wmdeletewin});

	static_assert(sizeof(cosmos::ProcessID) == 4, "NET_WM_PID requires a 32-bit pid type");
	xpp::Property<int> pid_prop(
			cosmos::to_integral(cosmos::proc::cached_pids.own_pid)
	);
	m_window.setProperty(getXAtom("_NET_WM_PID"), pid_prop);

	setDefaultTitle();
	setHints();
	m_display->mapWindow(m_window);
	m_display->sync();

	m_xsel.init();

	if (m_cmdline->useXSync()) {
		m_display->setSynchronized(true);
	}
}

std::tuple<XftFont*, FT_UInt> X11::lookupFontEntry(const Rune rune, Font &fnt, const FontFlags flags) {
	/* Lookup character index with default font. */
	auto glyphidx = XftCharIndex(*m_display, fnt.match, rune);
	if (glyphidx) {
		return std::make_tuple(fnt.match, glyphidx);
	}

	/* Fallback on font cache, search the font cache for match. */
	for (auto &fc: m_font_cache) {
		glyphidx = XftCharIndex(*m_display, fc.font, rune);
		/* Everything correct. */
		if (glyphidx && fc.flags == flags) {
			return std::make_tuple(fc.font, glyphidx);
		}
		/* We got a default font for a not found glyph. */
		else if (!glyphidx && fc.flags == flags && fc.unicodep == rune) {
			return std::make_tuple(fc.font, glyphidx);
		}
	}

	/* Nothing was found. Use fontconfig to find matching font. */
	FcResult fcres;
	if (!fnt.set)
		fnt.set = FcFontSort(0, fnt.pattern, 1, 0, &fcres);
	FcFontSet *fcsets[] = { NULL };
	fcsets[0] = fnt.set;

	/*
	 * Nothing was found in the cache. Now use some dozen
	 * of Fontconfig calls to get the font for one single
	 * character.
	 *
	 * Xft and fontconfig are design failures.
	 */
	FcPattern *fcpattern = FcPatternDuplicate(fnt.pattern);
	FcPatternGuard fcpattern_guard(fcpattern);
	FcCharSet *fccharset = FcCharSetCreate();
	FcCharSetGuard fccharset_guard(fccharset);

	FcCharSetAddChar(fccharset, rune);
	FcPatternAddCharSet(fcpattern, FC_CHARSET, fccharset);
	FcPatternAddBool(fcpattern, FC_SCALABLE, 1);

	FcConfigSubstitute(0, fcpattern, FcMatchPattern);
	FcDefaultSubstitute(fcpattern);

	FcPattern *fontpattern = FcFontSetMatch(0, fcsets, 1, fcpattern, &fcres);

	/* Allocate memory for the new cache entry. */

	auto font = XftFontOpenPattern(*m_display, fontpattern);
	if (!font)
		cosmos_throw (cosmos::ApiError("XftFontOpenPattern failed seeking fallback font"));
	m_font_cache.emplace_back(FontCache{font, flags, rune});

	auto &new_font = m_font_cache.back();

	glyphidx = XftCharIndex(*m_display, new_font.font, rune);

	return std::make_tuple(new_font.font, glyphidx);
}

size_t X11::makeGlyphFontSpecs(XftGlyphFontSpec *specs, const Glyph *glyphs, size_t len, const CharPos &loc) {
	Font *fnt = &m_draw_ctx.font;
	FontFlags fflags = FontFlags::NORMAL;
	const auto chr = m_twin.getChrExtent();
	auto runewidth = chr.width;
	size_t numspecs = 0;
	Glyph::AttrBitMask prevmode(Glyph::AttrBitMask::all);
	const auto start = m_twin.getDrawPos(loc);
	DrawPos cur(start.atBelow(fnt->ascent));

	for (size_t i = 0; i < len; i++) {
		/* Fetch rune and mode for current glyph. */
		Rune rune = glyphs[i].u;
		const auto &mode = glyphs[i].mode;

		/* Skip dummy wide-character spacing. */
		if (mode == Glyph::AttrBitMask({Attr::WDUMMY}))
			continue;

		/* Determine font for glyph if different from previous glyph. */
		if (prevmode != mode) {
			prevmode = mode;
			std::tie(fnt, fflags) = m_draw_ctx.getFontForMode(mode);
			runewidth = chr.width * (mode[Attr::WIDE] ? 2 : 1);
			cur.y = start.y + fnt->ascent;
		}

		auto [xftfont, glyphidx] = lookupFontEntry(rune, *fnt, fflags);

		auto &spec = specs[numspecs++];
		spec.font = xftfont;
		spec.glyph = glyphidx;
		spec.x = (short)cur.x;
		spec.y = (short)cur.y;
		cur.moveRight(runewidth);
	}

	return numspecs;
}

void X11::getGlyphColors(const Glyph base, FontColor &fg, FontColor &bg) {

	auto assignBaseColor = [this](FontColor &out, const Glyph::color_t col) {
		if (Glyph::isTrueColor(col)) {
			RenderColor tmp(col);
			XftColorAllocValue(*m_display, m_visual, m_color_map, &tmp, &out);
		} else {
			// col is a base index < 16
			out = m_draw_ctx.col[col];
		}
	};

	auto invertColor = [this](FontColor &c) {
		c.invert();
		RenderColor tmp(c);
		XftColorAllocValue(*m_display, m_visual, m_color_map, &tmp, &c);
	};

	assignBaseColor(fg, base.fg);
	assignBaseColor(bg, base.bg);

	/* Change basic system colors [0-7] to bright system colors [8-15] */
	if (base.needBrightColor() && base.isBasicColor())
		fg = m_draw_ctx.col[base.getBrightColor()];

	if (m_twin.inReverseMode()) {
		if (fg == m_draw_ctx.getDefaultFG()) {
			fg = m_draw_ctx.getDefaultBG();
		} else {
			invertColor(fg);
		}

		if (bg == m_draw_ctx.getDefaultBG()) {
			bg = m_draw_ctx.getDefaultFG();
		} else {
			invertColor(bg);
		}
	}

	if (base.needFaintColor()) {
		auto faint = fg.faint();
		RenderColor tmp(faint);
		XftColorAllocValue(*m_display, m_visual, m_color_map, &tmp, &fg);
	}

	if (base.mode[Attr::REVERSE]) {
		std::swap(fg, bg);
	}

	if (base.mode[Attr::BLINK] && m_twin.checkFlag(WinMode::BLINK))
		fg = bg;
	else if (base.mode[Attr::INVISIBLE])
		fg = bg;
}

void X11::drawGlyphFontSpecs(const XftGlyphFontSpec *specs, Glyph base, size_t len, const CharPos &loc) {

	FontColor fg, bg;
	m_draw_ctx.sanitizeColor(base);
	getGlyphColors(base, fg, bg);

	auto pos = m_twin.getDrawPos(loc);
	const auto &win = m_twin.getWinExtent();
	const auto &chr = m_twin.getChrExtent();
	const auto &tty = m_twin.getTTYExtent();
	const int textwidth = len * (base.mode[Attr::WIDE] ? 2 : 1) * chr.width;
	constexpr auto BORDERPX = config::BORDERPX;
	const bool reaches_bottom_border = pos.y + chr.height >= BORDERPX + tty.height;

	/* Intelligent cleaning up of the borders. */

	// left border
	if (loc.x == 0) {
		const auto pos1 = DrawPos{0, loc.y ? pos.y : 0};
		const auto pos2 = DrawPos{
			BORDERPX,
			pos.y + chr.height + (reaches_bottom_border ? win.height : 0)
		};
		clearRect(pos1, pos2);
	}

	// right border
	if (pos.x + textwidth >= BORDERPX + tty.width) {
		const auto pos1 = DrawPos{pos.x + textwidth, loc.y ? pos.y : 0};
		const auto pos2 = DrawPos{
			win.width,
			reaches_bottom_border ? win.height : pos.y + chr.height
		};
		clearRect(pos1, pos2);
	}

	// top border
	if (loc.y == 0)
		clearRect(DrawPos{pos.x, 0}, DrawPos{pos.x + textwidth, BORDERPX});

	// bottom border
	if (pos.y + chr.height >= BORDERPX + tty.height)
		clearRect(DrawPos{pos.x, pos.y + chr.height}, DrawPos{pos.x + textwidth, win.height});

	/* Clean up the region we want to draw to. */
	drawRect(bg, pos, Extent{textwidth, chr.height});

	/* Set the clip region because Xft is sometimes dirty. */
	XRectangle r{0, 0, static_cast<unsigned short>(textwidth), static_cast<unsigned short>(chr.height)};
	XftDrawSetClipRectangles(m_font_draw, pos.x, pos.y, &r, 1);

	/* Render the glyphs. */
	XftDrawGlyphFontSpec(m_font_draw, &fg, specs, len);

	/* Render underline and strikethrough. */
	if (base.mode[Attr::UNDERLINE]) {
		drawRect(fg, pos.atBelow(m_draw_ctx.font.ascent + 1), Extent{textwidth, 1});
	}

	if (base.mode[Attr::STRUCK]) {
		drawRect(fg, pos.atBelow(2 * m_draw_ctx.font.ascent / 3), Extent{textwidth, 1});
	}

	/* Reset clip to none. */
	XftDrawSetClip(m_font_draw, 0);
}

void X11::drawGlyph(Glyph g, const CharPos &loc) {
	XftGlyphFontSpec spec;

	auto numspecs = makeGlyphFontSpecs(&spec, &g, 1, loc);
	drawGlyphFontSpecs(&spec, g, numspecs, loc);
}

void X11::clearCursor(const CharPos &pos, Glyph glyph) {
	/* remove the old cursor */
	if (m_nst.getSelection().isSelected(pos))
		glyph.mode.flip(Attr::REVERSE);
	drawGlyph(glyph, pos);
}

const FontColor& X11::getCursorColor(const CharPos &pos, Glyph &glyph) const {
	const bool is_selected = m_nst.getSelection().isSelected(pos);

	/*
	 * Select the right color for the right mode.
	 */
	glyph.mode.limit({Attr::BOLD, Attr::ITALIC, Attr::UNDERLINE, Attr::STRUCK, Attr::WIDE});

	if (m_twin.inReverseMode()) {
		glyph.mode.set(Attr::REVERSE);
		glyph.bg = config::DEFAULTFG;
		if (is_selected) {
			glyph.fg = config::DEFAULTRCS;
			return m_draw_ctx.col[config::DEFAULTCS];
		} else {
			glyph.fg = config::DEFAULTCS;
			return m_draw_ctx.col[config::DEFAULTRCS];
		}
	} else {
		if (is_selected) {
			glyph.fg = config::DEFAULTFG;
			glyph.bg = config::DEFAULTRCS;
		} else {
			glyph.fg = config::DEFAULTBG;
			glyph.bg = config::DEFAULTCS;
		}

		return m_draw_ctx.col[glyph.bg];
	}
}

void X11::drawCursor(const CharPos &pos, Glyph glyph) {

	if (m_twin.checkFlag(WinMode::HIDE_CURSOR))
		return;

	auto &drawcol = getCursorColor(pos, glyph);
	auto &chr = m_twin.getChrExtent();
	constexpr auto CURSORTHICKNESS = config::CURSORTHICKNESS;

	/* draw the new one */
	if (m_twin.checkFlag(WinMode::FOCUSED)) {
		switch (m_twin.getCursorStyle()) {
		case CursorStyle::SNOWMAN: /* st extension */
			glyph.u = 0x2603; /* snowman (U+2603) */
		/* FALLTHROUGH */
		case CursorStyle::BLINKING_BLOCK:
		case CursorStyle::BLINKING_BLOCK_DEFAULT:
		case CursorStyle::STEADY_BLOCK:
			drawGlyph(glyph, pos);
			break;
		case CursorStyle::BLINKING_UNDERLINE:
		case CursorStyle::STEADY_UNDERLINE: {
			auto dpos = m_twin.getDrawPos(pos.nextLine());
			dpos.moveUp(CURSORTHICKNESS);
			drawRect(drawcol, dpos, Extent{chr.width, CURSORTHICKNESS});
			break;
		}
		case CursorStyle::BLINKING_BAR:
		case CursorStyle::STEADY_BAR: {
			auto dpos = m_twin.getDrawPos(pos);
			drawRect(drawcol, dpos, Extent{CURSORTHICKNESS, chr.height});
			break;
		}
		default:
			break;
		}
	} else {
		// only draw a non-solid rectangle outline of the cursor, if
		// there's no focus
		auto dpos = m_twin.getDrawPos(pos);
		drawRect(drawcol, dpos, Extent{chr.width - 1, 1});
		drawRect(drawcol, dpos, Extent{1, chr.height - 1});

		auto nextcol = m_twin.getNextCol(dpos).atLeft(1);
		drawRect(drawcol, nextcol, Extent{1, chr.height - 1});

		auto nextline = m_twin.getNextLine(dpos).atAbove(1);
		drawRect(drawcol, nextline, Extent{chr.width, 1});
	}
}

void X11::setDefaultIconTitle() {
	setIconTitle(m_cmdline->getTitle());
}

void X11::setIconTitle(const std::string &title) {
	xpp::Property<xpp::utf8_string> data{xpp::utf8_string(title)};
	m_window.setProperty(xpp::XAtom{XA_WM_ICON_NAME}, data);
	m_window.setProperty(m_netwmiconname, data);
}

void X11::setDefaultTitle() {
	setTitle(m_cmdline->getTitle());
}

void X11::setTitle(const std::string &title) {
	xpp::Property<xpp::utf8_string> data{xpp::utf8_string(title)};
	m_window.setProperty(m_wmname, data);
	m_window.setProperty(m_netwmname, data);
}

void X11::drawLine(const Line &line, const CharPos &start, const int count) {
	Glyph base, newone;
	auto *specs = m_font_specs.data();
	size_t numcols = 0;
	CharPos curpos{0, start.y};
	auto &selection = m_nst.getSelection();

	auto numspecs = makeGlyphFontSpecs(specs, &line[start.x], count, start);
	for (int x = start.x; x < start.x + count && numcols < numspecs; x++) {
		newone = line[x];
		if (newone.mode.only(Attr::WDUMMY))
			continue;
		if (selection.isSelected(CharPos{x, start.y}))
			newone.mode.flip(Attr::REVERSE);
		if (numcols > 0 && base.attrsDiffer(newone)) {
			drawGlyphFontSpecs(specs, base, numcols, curpos);
			specs += numcols;
			numspecs -= numcols;
			numcols = 0;
		}
		if (numcols == 0) {
			curpos.x = x;
			base = newone;
		}
		numcols++;
	}
	if (numcols > 0)
		drawGlyphFontSpecs(specs, base, numcols, curpos);
}

void X11::finishDraw() {
	auto extent = m_twin.getWinExtent();
	m_window.copyArea(m_draw_ctx.getGC(), m_pixmap, extent);
	m_draw_ctx.setForeground(m_twin.getActiveForegroundColor());
}

void X11::changeEventMask(long event, bool on_off) {
	modifyBit(m_win_attrs.event_mask, on_off ? 1 : 0, event);
	m_window.setWindowAttrs(m_win_attrs, xpp::WindowAttrMask{xpp::WindowAttr::EventMask});
}

void X11::setPointerMotion(bool on_off) {
	modifyBit(m_win_attrs.event_mask, on_off, PointerMotionMask);
	m_window.setWindowAttrs(m_win_attrs, xpp::WindowAttrMask{xpp::WindowAttr::EventMask});
}

void X11::setMode(const WinMode &flag, const bool set) {
	const auto prevmode = m_twin.getMode();
	m_twin.setFlag(flag, set);
	if (m_twin.checkFlag(WinMode::REVERSE) != prevmode[WinMode::REVERSE])
		m_nst.getTerm().redraw();
}

void X11::setCursorStyle(const CursorStyle &cursor) {
	m_twin.setCursorStyle(cursor);
}

void X11::setUrgency(int add) {
	// should never be nullptr, since we've set hints initially
	auto hints = m_window.getWMHints();

	modifyBit(hints->flags, add, XUrgencyHint);

	m_window.setWMHints(*hints);
}

void X11::ringBell() {
	if (!(m_twin.checkFlag(WinMode::FOCUSED)))
		setUrgency(1);
	if (config::BELLVOLUME)
		XkbBell(*m_display, m_window, config::BELLVOLUME, (Atom)nullptr);
}

void X11::embeddedFocusChange(const bool in_focus) {
	// called when we run embedded in another window and the focus
	// changes
	if (in_focus) {
		m_twin.setFlag(WinMode::FOCUSED);
		setUrgency(0);
	} else {
		m_twin.resetFlag(WinMode::FOCUSED);
	}
}

void X11::focusChange(const bool in_focus) {
	if (in_focus) {
		m_input.setFocus();
		m_twin.setFlag(WinMode::FOCUSED);
		setUrgency(0);
	} else {
		m_input.unsetFocus();
		m_twin.resetFlag(WinMode::FOCUSED);
	}

	if (m_twin.checkFlag(WinMode::FOCUS)) {
		// called when focus changes and we run in our own window
		m_nst.getTerm().reportFocus(in_focus);
	}
}

} // end ns nst
