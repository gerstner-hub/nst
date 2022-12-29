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
#include "cosmos/errors/ApiError.hxx"
#include "cosmos/errors/RuntimeError.hxx"
#include "cosmos/formatting.hxx"
#include "cosmos/proc/Process.hxx"

// X++
#include "X++/Event.hxx"
#include "X++/RootWin.hxx"
#include "X++/XDisplay.hxx"
#include "X++/Xpp.hxx"

// nst
#include "codecs.hxx"
#include "helper.hxx"
/* nst_config.h for applying patches and the configuration. */
#include "nst_config.hxx"
#include "nst.hxx"
#include "Selection.hxx"
#include "Term.hxx"
#include "TTY.hxx"
#include "types.hxx"
#include "x11.hxx"
#include "XSelection.hxx"

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

std::tuple<Font*, FRC> DrawingContext::getFontForMode(const Glyph::AttrBitMask &mode) {
	if (mode.allOf({Attr::ITALIC, Attr::BOLD})) {
		return std::make_tuple(&ibfont, FRC::ITALICBOLD);
	} else if (mode[Attr::ITALIC]) {
		return std::make_tuple(&ifont, FRC::ITALIC);
	} else if (mode[Attr::BOLD]) {
		return std::make_tuple(&bfont, FRC::BOLD);
	} else {
		return std::make_tuple(&font, FRC::NORMAL);
	}
}

void DrawingContext::setForeground(const Color &color) {
	XSetForeground(*m_display, getRawGC(), color.pixel);
}

void DrawingContext::fillRectangle(const DrawPos &pos, const Extent &ext) {
	XFillRectangle(*m_display, m_pixmap.id(), getRawGC(), pos.x, pos.y, ext.width, ext.height);
}

X11::X11(Nst &nst) : m_nst(nst), m_input(*this), m_xsel(nst), m_tsize{config::COLS, config::ROWS} {
	m_tsize.normalize();
	setCursorStyle(config::CURSORSHAPE);
}

X11::~X11() {
	m_draw_ctx.freeGC();
}

void X11::copyToClipboard() {
	m_xsel.copyPrimaryToClipboard();

	if (m_xsel.havePrimarySelection()) {
		Atom clipboard = m_mapper->getAtom("CLIPBOARD");
		XSetSelectionOwner(*m_display, clipboard, m_window, CurrentTime);
	}
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

int X11::loadColor(size_t i, const char *name, Color *ncolor) {
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
		} else
			name = config::getColorName(i);
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
			auto colorname = config::getColorName(i);
			if (colorname)
				cosmos_throw (cosmos::ApiError(cosmos::sprintf("could not allocate color '%s'", colorname)));
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

	Color ncolor;
	if (!loadColor(idx, name, &ncolor))
		return false;

	XftColorFree(*m_display, m_visual, m_color_map, &m_draw_ctx.col[idx]);
	m_draw_ctx.col[idx] = ncolor;

	return true;
}

/*
 * Absolute coordinates.
 */
void X11::clearRect(const DrawPos &pos1, const DrawPos &pos2) {
	const auto colindex = m_twin.getActiveForegroundColor();
	XftDrawRect(m_font_draw, &m_draw_ctx.col[colindex], pos1.x, pos1.y, pos2.x - pos1.x, pos2.y - pos1.y);
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
	XSizeHints *sizeh = XAllocSizeHints();

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
		sizeh->x = m_left_offset;
		sizeh->y = m_top_offset;
		sizeh->win_gravity = getGravity();
	}

	XSetWMProperties(*m_display, m_window, NULL, NULL, NULL, 0, sizeh, &wm, &clazz);
	XFree(sizeh);
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
	XftFontClose(*m_display, f->match);
	FcPatternDestroy(f->pattern);
	if (f->set)
		FcFontSetDestroy(f->set);
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

void X11::setGeometry(const std::string &g) {
	unsigned int cols, rows;

	m_geometry = XParseGeometry(g.c_str(), &m_left_offset, &m_top_offset, &cols, &rows);

	m_tsize.rows = rows;
	m_tsize.cols = cols;
	m_twin.setWinExtent(m_tsize);
	const auto &win = m_twin.getWinExtent();
	if (m_geometry & XNegative)
		m_left_offset += m_display->getDisplayWidth(m_screen) - win.width - 2;
	if (m_geometry & YNegative)
		m_top_offset  += m_display->getDisplayHeight(m_screen) - win.height - 2;
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
		auto res = XParseColor(*m_display, m_color_map, config::getColorName(colnr), &out);
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
		xpp::WindowSpec{m_left_offset, m_top_offset,
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

	m_wmdeletewin = getAtom("WM_DELETE_WINDOW");
	m_netwmname = getAtom("_NET_WM_NAME");
	m_netwmiconname = getAtom("_NET_WM_ICON_NAME");
	XSetWMProtocols(*m_display, m_window, &m_wmdeletewin, 1);

	static_assert(sizeof(cosmos::ProcessID) == 4, "NET_WM_PID requires a 32-bit pid type");
	xpp::Property<int> pid_prop(cosmos::g_process.getPid());
	m_window.setProperty(getAtom("_NET_WM_PID"), pid_prop);

	setDefaultTitle();
	setHints();
	m_display->mapWindow(m_window);
	m_display->sync();

	m_xsel.init();

	if (m_cmdline->useXSync()) {
		m_display->setSynchronized(true);
	}
}

size_t X11::makeGlyphFontSpecs(XftGlyphFontSpec *specs, const Glyph *glyphs, size_t len, int x, int y) {
	const auto pos = m_twin.getDrawPos(CharPos{x,y});
	Font *fnt = &m_draw_ctx.font;
	FRC frcflags = FRC::NORMAL;
	const auto chr = m_twin.getChrExtent();
	int runewidth = chr.width;
	size_t numspecs = 0;
	Glyph::AttrBitMask prevmode(Glyph::AttrBitMask::all);

	for (size_t i = 0, xp = pos.x, yp = pos.y + fnt->ascent; i < len; ++i) {
		/* Fetch rune and mode for current glyph. */
		Rune rune = glyphs[i].u;
		const auto &mode = glyphs[i].mode;

		/* Skip dummy wide-character spacing. */
		if (mode == Glyph::AttrBitMask({Attr::WDUMMY}))
			continue;

		/* Determine font for glyph if different from previous glyph. */
		if (prevmode != mode) {
			prevmode = mode;
			std::tie(fnt, frcflags) = m_draw_ctx.getFontForMode(mode);
			runewidth = chr.width * (mode[Attr::WIDE] ? 2 : 1);
			yp = pos.y + fnt->ascent;
		}

		/* Lookup character index with default font. */
		auto glyphidx = XftCharIndex(*m_display, fnt->match, rune);
		if (glyphidx) {
			auto &spec = specs[numspecs];
			spec.font = fnt->match;
			spec.glyph = glyphidx;
			spec.x = (short)xp;
			spec.y = (short)yp;
			xp += runewidth;
			numspecs++;
			continue;
		}

		Fontcache *font_entry = nullptr;
		/* Fallback on font cache, search the font cache for match. */
		for (auto &fc: m_font_cache) {
			glyphidx = XftCharIndex(*m_display, fc.font, rune);
			/* Everything correct. */
			if (glyphidx && fc.flags == frcflags) {
				font_entry = &fc;
				break;
			}
			/* We got a default font for a not found glyph. */
			else if (!glyphidx && fc.flags == frcflags && fc.unicodep == rune) {
				font_entry = &fc;
				break;
			}
		}

		/* Nothing was found. Use fontconfig to find matching font. */
		if (!font_entry) {
			FcResult fcres;
			if (!fnt->set)
				fnt->set = FcFontSort(0, fnt->pattern, 1, 0, &fcres);
			FcFontSet *fcsets[] = { NULL };
			fcsets[0] = fnt->set;

			/*
			 * Nothing was found in the cache. Now use some dozen
			 * of Fontconfig calls to get the font for one single
			 * character.
			 *
			 * Xft and fontconfig are design failures.
			 */
			FcPattern *fcpattern = FcPatternDuplicate(fnt->pattern);
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
			m_font_cache.emplace_back(Fontcache{font, frcflags, rune});

			glyphidx = XftCharIndex(*m_display, m_font_cache.back().font, rune);

			font_entry = &m_font_cache.back();
		}

		auto &spec = specs[numspecs];
		spec.font = font_entry->font;
		spec.glyph = glyphidx;
		spec.x = (short)xp;
		spec.y = (short)yp;
		xp += runewidth;
		numspecs++;
	}

	return numspecs;
}

static void setRenderColor(XRenderColor &out, const uint32_t in) {
	/* seems like the X color values are 16-bit wide and we need to
	 * translate the one color bytes into the upper byte in the
	 * XRenderColor */
	out.alpha = 0xffff;
	out.red = (in & 0xff0000) >> 8;
	out.green = (in & 0xff00);
	out.blue = (in & 0xff) << 8;
}

void X11::drawGlyphFontSpecs(const XftGlyphFontSpec *specs, Glyph base, size_t len, int x, int y) {
	const size_t charlen = len * (base.mode[Attr::WIDE] ? 2 : 1);

	/* Fallback on color display for attributes not supported by the font */
	if (base.mode[Attr::ITALIC] && base.mode[Attr::BOLD]) {
		if (m_draw_ctx.ibfont.badslant || m_draw_ctx.ibfont.badweight)
			base.fg = config::DEFAULTATTR;
	} else if ((base.mode[Attr::ITALIC] && m_draw_ctx.ifont.badslant) ||
	    (base.mode[Attr::BOLD] && m_draw_ctx.bfont.badweight)) {
		base.fg = config::DEFAULTATTR;
	}

	Color *fg = nullptr;
	XRenderColor colfg;
	Color truefg;

	if (base.isFgTrueColor()) {
		setRenderColor(colfg, base.fg);
		XftColorAllocValue(*m_display, m_visual, m_color_map, &colfg, &truefg);
		fg = &truefg;
	} else {
		fg = &m_draw_ctx.col[base.fg];
	}

	Color *bg = nullptr;
	XRenderColor colbg;
	Color truebg;

	if (base.isBgTrueColor()) {
		setRenderColor(colbg, base.bg);
		XftColorAllocValue(*m_display, m_visual, m_color_map, &colbg, &truebg);
		bg = &truebg;
	} else {
		bg = &m_draw_ctx.col[base.bg];
	}

	/* Change basic system colors [0-7] to bright system colors [8-15] */
	if (base.mode[Attr::BOLD] && !base.mode[Attr::FAINT] && base.fg <= 7)
		fg = &m_draw_ctx.col[base.fg + 8];

	Color revfg, revbg;
	if (m_twin.checkFlag(WinMode::REVERSE)) {
		if (fg == &m_draw_ctx.col[config::DEFAULTFG]) {
			fg = &m_draw_ctx.col[config::DEFAULTBG];
		} else {
			auto inverted = fg->inverted();
			inverted.assignTo(colfg);
			XftColorAllocValue(*m_display, m_visual, m_color_map, &colfg, &revfg);
			fg = &revfg;
		}

		if (bg == &m_draw_ctx.col[config::DEFAULTBG]) {
			bg = &m_draw_ctx.col[config::DEFAULTFG];
		} else {
			auto inverted = bg->inverted();
			inverted.assignTo(colbg);
			XftColorAllocValue(*m_display, m_visual, m_color_map, &colbg, &revbg);
			bg = &revbg;
		}
	}

	if (base.mode[Attr::FAINT] && !base.mode[Attr::BOLD]) {
		auto faint = fg->faint();
		faint.assignTo(colfg);
		XftColorAllocValue(*m_display, m_visual, m_color_map, &colfg, &revfg);
		fg = &revfg;
	}

	if (base.mode[Attr::REVERSE]) {
		std::swap(fg, bg);
	}

	if (base.mode[Attr::BLINK] && m_twin.checkFlag(WinMode::BLINK))
		fg = bg;
	else if (base.mode[Attr::INVISIBLE])
		fg = bg;

	/* Intelligent cleaning up of the borders. */
	auto pos = m_twin.getDrawPos({x, y});
	const auto &win = m_twin.getWinExtent();
	const auto &chr = m_twin.getChrExtent();
	const auto &tty = m_twin.getTTYExtent();
	int width = charlen * chr.width;

	if (x == 0) {
		const auto pos1 = DrawPos{0, y ? pos.y : 0};
		const auto pos2 = DrawPos{config::BORDERPX, pos.y + chr.height +
			((pos.y + chr.height >= config::BORDERPX + tty.height) ? win.height : 0)};
		clearRect(pos1, pos2);
	}

	if (pos.x + width >= config::BORDERPX + tty.width) {
		const auto pos1 = DrawPos{pos.x + width, y ? pos.y : 0};
		const auto pos2 = DrawPos{win.width,
			(pos.y + chr.height >= config::BORDERPX + tty.height) ? win.height : (pos.y + chr.height)};
		clearRect(pos1, pos2);
	}

	if (y == 0)
		clearRect(DrawPos{pos.x, 0}, DrawPos{pos.x + width, config::BORDERPX});

	if (pos.y + chr.height >= config::BORDERPX + tty.height)
		clearRect(DrawPos{pos.x, pos.y + chr.height}, DrawPos{pos.x + width, win.height});

	/* Clean up the region we want to draw to. */
	XftDrawRect(m_font_draw, bg, pos.x, pos.y, width, chr.height);

	/* Set the clip region because Xft is sometimes dirty. */
	XRectangle r;
	r.x = 0;
	r.y = 0;
	r.height = chr.height;
	r.width = width;
	XftDrawSetClipRectangles(m_font_draw, pos.x, pos.y, &r, 1);

	/* Render the glyphs. */
	XftDrawGlyphFontSpec(m_font_draw, fg, specs, len);

	/* Render underline and strikethrough. */
	if (base.mode[Attr::UNDERLINE]) {
		XftDrawRect(m_font_draw, fg, pos.x, pos.y + m_draw_ctx.font.ascent + 1, width, 1);
	}

	if (base.mode[Attr::STRUCK]) {
		XftDrawRect(m_font_draw, fg, pos.x, pos.y + 2 * m_draw_ctx.font.ascent / 3, width, 1);
	}

	/* Reset clip to none. */
	XftDrawSetClip(m_font_draw, 0);
}

void X11::drawGlyph(Glyph g, int x, int y) {
	XftGlyphFontSpec spec;

	auto numspecs = makeGlyphFontSpecs(&spec, &g, 1, x, y);
	drawGlyphFontSpecs(&spec, g, numspecs, x, y);
}

void X11::drawCursor(int cx, int cy, Glyph g, int ox, int oy, Glyph og) {
	auto &sel = m_nst.getSelection();

	/* remove the old cursor */
	if (sel.isSelected(ox, oy))
		og.mode.flip(Attr::REVERSE);
	drawGlyph(og, ox, oy);

	if (m_twin.checkFlag(WinMode::HIDE))
		return;

	/*
	 * Select the right color for the right mode.
	 */
	g.mode.limit({Attr::BOLD, Attr::ITALIC, Attr::UNDERLINE, Attr::STRUCK, Attr::WIDE});
	Color drawcol;

	if (m_twin.checkFlag(WinMode::REVERSE)) {
		g.mode.set(Attr::REVERSE);
		g.bg = config::DEFAULTFG;
		if (sel.isSelected(cx, cy)) {
			drawcol = m_draw_ctx.col[config::DEFAULTCS];
			g.fg = config::DEFAULTRCS;
		} else {
			drawcol = m_draw_ctx.col[config::DEFAULTRCS];
			g.fg = config::DEFAULTCS;
		}
	} else {
		if (sel.isSelected(cx, cy)) {
			g.fg = config::DEFAULTFG;
			g.bg = config::DEFAULTRCS;
		} else {
			g.fg = config::DEFAULTBG;
			g.bg = config::DEFAULTCS;
		}
		drawcol = m_draw_ctx.col[g.bg];
	}

	auto &chr = m_twin.getChrExtent();

	/* draw the new one */
	if (m_twin.checkFlag(WinMode::FOCUSED)) {
		switch (m_twin.getCursorStyle()) {
		case CursorStyle::SNOWMAN: /* st extension */
			g.u = 0x2603; /* snowman (U+2603) */
		/* FALLTHROUGH */
		case CursorStyle::BLINKING_BLOCK:
		case CursorStyle::BLINKING_BLOCK_DEFAULT:
		case CursorStyle::STEADY_BLOCK:
			drawGlyph(g, cx, cy);
			break;
		case CursorStyle::BLINKING_UNDERLINE:
		case CursorStyle::STEADY_UNDERLINE: {
			auto pos = m_twin.getDrawPos(CharPos{cx,cy+1});
			XftDrawRect(
					m_font_draw, &drawcol,
					pos.x, pos.y - config::CURSORTHICKNESS,
					chr.width, config::CURSORTHICKNESS);
			break;
		}
		case CursorStyle::BLINKING_BAR:
		case CursorStyle::STEADY_BAR: {
			auto pos = m_twin.getDrawPos(CharPos{cx, cy});
			XftDrawRect(
					m_font_draw, &drawcol,
					pos.x, pos.y,
					config::CURSORTHICKNESS, chr.height);
			break;
		}
		default:
			break;
		}
	} else {
		auto pos = m_twin.getDrawPos(CharPos{cx, cy});

		XftDrawRect(m_font_draw, &drawcol,
				pos.x,
				pos.y,
				chr.width - 1, 1);
		XftDrawRect(m_font_draw, &drawcol,
				pos.x,
				pos.y,
				1, chr.height - 1);
		XftDrawRect(m_font_draw, &drawcol,
				m_twin.getNextCol(pos).x - 1,
				pos.y,
				1, chr.height - 1);
		XftDrawRect(m_font_draw, &drawcol,
				pos.x,
				m_twin.getNextLine(pos).y - 1,
				chr.width, 1);
	}
}

void X11::setDefaultIconTitle() {
	setIconTitle(m_cmdline->getTitle());
}

void X11::setIconTitle(const std::string &title) {
	XTextProperty prop;
	// the API forces us to unconst this, the parameter should not be
	// modified though
	char *titlep = const_cast<char*>(title.c_str());

	if (Xutf8TextListToTextProperty(*m_display, &titlep, 1, XUTF8StringStyle,
	                                &prop) != Success)
		return;
	XSetWMIconName(*m_display, m_window, &prop);
	XSetTextProperty(*m_display, m_window, &prop, m_netwmiconname);
	XFree(prop.value);
}

void X11::setDefaultTitle() {
	setTitle(m_cmdline->getTitle());
}

void X11::setTitle(const std::string &title) {
	XTextProperty prop;
	char *titlep = const_cast<char*>(title.c_str());

	if (Xutf8TextListToTextProperty(*m_display, &titlep, 1, XUTF8StringStyle,
	                                &prop) != Success)
		return;
	XSetWMName(*m_display, m_window, &prop);
	XSetTextProperty(*m_display, m_window, &prop, m_netwmname);
	XFree(prop.value);
}

void X11::drawLine(const Line &line, int x1, int y1, int x2) {
	Glyph base, newone;
	auto *specs = m_font_specs.data();
	size_t i = 0;
	int ox = 0;
	auto &selection = m_nst.getSelection();

	auto numspecs = makeGlyphFontSpecs(specs, &line[x1], x2 - x1, x1, y1);
	for (int x = x1; x < x2 && i < numspecs; x++) {
		newone = line[x];
		if (newone.mode.only(Attr::WDUMMY))
			continue;
		if (selection.isSelected(x, y1))
			newone.mode.flip(Attr::REVERSE);
		if (i > 0 && base.attrsDiffer(newone)) {
			drawGlyphFontSpecs(specs, base, i, ox, y1);
			specs += i;
			numspecs -= i;
			i = 0;
		}
		if (i == 0) {
			ox = x;
			base = newone;
		}
		i++;
	}
	if (i > 0)
		drawGlyphFontSpecs(specs, base, i, ox, y1);
}

void X11::finishDraw() {
	auto win = m_twin.getWinExtent();
	XCopyArea(*m_display, m_pixmap.id(), m_window, m_draw_ctx.getRawGC(), 0, 0, win.width, win.height, 0, 0);
	m_draw_ctx.setForeground(m_twin.getActiveForegroundColor());
}

void X11::changeEventMask(long event, bool on_off) {
	modifyBit(m_win_attrs.event_mask, on_off ? 1 : 0, event);
	XChangeWindowAttributes(*m_display, m_window, CWEventMask, &m_win_attrs);
}

void X11::setPointerMotion(bool on_off) {
	modifyBit(m_win_attrs.event_mask, on_off, PointerMotionMask);
	XChangeWindowAttributes(*m_display, m_window, CWEventMask, &m_win_attrs);
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
	XWMHints *h = XGetWMHints(*m_display, m_window);

	modifyBit(h->flags, add, XUrgencyHint);
	XSetWMHints(*m_display, m_window, h);
	XFree(h);
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
	// called when focus changes and we run in our own window
	if (in_focus) {
		m_input.setFocus();
		m_twin.setFlag(WinMode::FOCUSED);
		setUrgency(0);
		if (m_twin.checkFlag(WinMode::FOCUS)) {
			m_nst.getTTY().write("\033[I", 3, 0);
		}
	} else {
		m_input.unsetFocus();
		m_twin.resetFlag(WinMode::FOCUSED);
		if (m_twin.checkFlag(WinMode::FOCUS))
			m_nst.getTTY().write("\033[O", 3, 0);
	}
}

} // end ns nst
