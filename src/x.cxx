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
#include <map>
#include <string>

// cosmos
#include "cosmos/algs.hxx"
#include "cosmos/types.hxx"
#include "cosmos/errors/ApiError.hxx"
#include "cosmos/errors/RuntimeError.hxx"
#include "cosmos/formatting.hxx"
#include "cosmos/io/Poller.hxx"
#include "cosmos/proc/Process.hxx"
#include "cosmos/time/TimeSpec.hxx"

// X++
#include "X++/Event.hxx"
#include "X++/RootWin.hxx"
#include "X++/XDisplay.hxx"
#include "X++/Xpp.hxx"

// nst
/* nst_config.h for applying patches and the configuration. */
#include "nst_config.h"
#include "Cmdline.hxx"
#include "codecs.hxx"
#include "helper.hxx"
#include "nst.hxx"
#include "types.hxx"
#include "Selection.hxx"
#include "Term.hxx"
#include "TTY.hxx"
#include "win.h"
#include "x.hxx"
#include "XSelection.hxx"

// config parts specific to this compilation unit
#include "nst_config.inl"

namespace nst {

namespace {

/* Globals */
X11 x11;
TermWindow twin;
TermSize tsize{config::COLS, config::ROWS};

} // end anon ns

Nst *Nst::the_instance = nullptr;

void X11::copyToClipboard() {
	m_xsel.copyPrimaryToClipboard();

	if (m_xsel.havePrimarySelection()) {
		Atom clipboard = m_mapper->getAtom("CLIPBOARD");
		XSetSelectionOwner(*m_display, clipboard, m_window, CurrentTime);
	}
}

void xclipcopy(void) {
	x11.copyToClipboard();
}

void X11::pasteClipboard() {
	Atom clipboard = getAtom("CLIPBOARD");
	XConvertSelection(*m_display, clipboard, m_xsel.getTargetFormat(), clipboard,
			m_window, CurrentTime);
}

void X11::pasteSelection() {
	XConvertSelection(
		*m_display, XA_PRIMARY, m_xsel.getTargetFormat(), XA_PRIMARY,
		m_window, CurrentTime);
}

void X11::toggleNumlock() {
	twin.mode.flip(WinMode::NUMLOCK);
}

void X11::zoomFont(float val) {
	val += (float)m_used_font_size;
	unloadFonts();
	loadFontsOrThrow(m_cmdline->font.getValue(), val);
	auto &nst = Nst::getInstance();
	nst.resizeConsole();
	nst.getTerm().redraw();
	setHints();
}

void X11::resetFont() {
	if (m_default_font_size > 0) {
		m_used_font_size = m_default_font_size;
		zoomFont(0);
	}
}

const char* getColorName(size_t nr) {
	if (nr < config::COLORNAMES.size())
		return config::COLORNAMES[nr];
	else if (nr < 256)
		// unassigned
		return nullptr;
	else {
		// check for extended colors
		nr -= 256;
		if (nr < config::EXTENDED_COLORS.size())
			return config::EXTENDED_COLORS[nr];
	}

	return nullptr;
}

void xsetsel(const char *str) {
	x11.getXSelection().setSelection(str);
}

void Nst::resizeConsole(const Extent &win) {

	twin.setWinExtent(win);

	auto tdim = twin.getTermDim();

	m_term.resize(tdim.cols, tdim.rows);
	x11.resize(tdim);
	m_tty.resize(twin.tty);
}

void X11::resize(const TermSize &dim) {

	twin.setTermDim(dim);

	XFreePixmap(*m_display, m_draw_buf);
	m_draw_buf = XCreatePixmap(
		*m_display,
		m_window,
		twin.win.width, twin.win.height,
		DefaultDepth(getRawDisplay(), m_screen)
	);
	XftDrawChange(m_font_draw, m_draw_buf);
	clearRect(DrawPos{0,0}, DrawPos{twin.win.width, twin.win.height});

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
			name = getColorName(i);
	}

	return XftColorAllocName(*m_display, m_visual, m_color_map, name, ncolor);
}

void xloadcols() {
	x11.loadColors();
}

void X11::loadColors() {
	if (m_colors_loaded) {
		for (auto &c: m_draw_ctx.col) {
			XftColorFree(*m_display, m_visual, m_color_map, &c);
		}
	} else {
		auto len = std::max(256UL + config::EXTENDED_COLORS.size(), 256UL);
		m_draw_ctx.col.resize(len);
	}

	for (size_t i = 0; i < m_draw_ctx.col.size(); i++) {
		if (!loadColor(i, nullptr, &m_draw_ctx.col[i])) {
			auto colorname = getColorName(i);
			if (colorname)
				cosmos_throw (cosmos::ApiError(cosmos::sprintf("could not allocate color '%s'", colorname)));
			else
				cosmos_throw (cosmos::ApiError(cosmos::sprintf("could not allocate color %zd", i)));
		}
	}

	m_colors_loaded = true;
}

int xgetcolor(size_t x, unsigned char *r, unsigned char *g, unsigned char *b) {
	return x11.getColor(x, r, g, b) ? 0 : 1;
}

bool X11::getColor(size_t idx, unsigned char *r, unsigned char *g, unsigned char *b) const {
	if (idx >= m_draw_ctx.col.size())
		return false;

	*r = m_draw_ctx.col[idx].color.red >> 8;
	*g = m_draw_ctx.col[idx].color.green >> 8;
	*b = m_draw_ctx.col[idx].color.blue >> 8;

	return true;
}

int xsetcolorname(size_t x, const char *name) {
	return x11.setColorName(x, name) ? 0 : 1;
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
	const auto colindex = twin.mode[WinMode::REVERSE] ? config::DEFAULTFG : config::DEFAULTBG;
	XftDrawRect(m_font_draw, &m_draw_ctx.col[colindex], pos1.x, pos1.y, pos2.x - pos1.x, pos2.y - pos1.y);
}

void X11::setHints() {
	// note: the X API breaks constness here, thus use a by-value-copy of
	// the command line arguments
	auto wname = m_cmdline->window_name.getValue();
	auto wclass = m_cmdline->window_class.getValue();
	XClassHint clazz = {&wname[0], &wclass[0]};
	XWMHints wm = {InputHint, 1, 0, 0, 0, 0, 0, 0, 0};
	XSizeHints *sizeh = XAllocSizeHints();

	sizeh->flags = PSize | PResizeInc | PBaseSize | PMinSize;
	sizeh->height = twin.win.height;
	sizeh->width = twin.win.width;
	sizeh->height_inc = twin.chr.height;
	sizeh->width_inc = twin.chr.width;
	sizeh->base_height = 2 * config::BORDERPX;
	sizeh->base_width = 2 * config::BORDERPX;
	sizeh->min_height = twin.chr.height + 2 * config::BORDERPX;
	sizeh->min_width = twin.chr.width + 2 * config::BORDERPX;
	if (m_fixed_geometry) {
		sizeh->flags |= PMaxSize;
		sizeh->min_width = sizeh->max_width = twin.win.width;
		sizeh->min_height = sizeh->max_height = twin.win.height;
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
	twin.setCharSize(m_draw_ctx.font);

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
		// like x11.win even with conversion operator does not work
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

	const auto dp = twin.getDrawPos(chp.nextLine());

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

	m_geometry = XParseGeometry(
			g.c_str(), &m_left_offset, &m_top_offset, &cols, &rows);

	tsize.rows = rows;
	tsize.cols = cols;
}

void X11::init() {
	m_cmdline = &Nst::getInstance().getCmdline();
	m_display = &xpp::XDisplay::getInstance();
	m_mapper = &xpp::XAtomMapper::getInstance();
	m_screen = m_display->getDefaultScreen();
	m_visual = m_display->getDefaultVisual(m_screen);

	/* font */
	if (!FcInit())
		cosmos_throw (cosmos::RuntimeError("could not init fontconfig"));

	loadFontsOrThrow(m_cmdline->font.getValue());

	/* colors */
	m_color_map = m_display->getDefaultColormap(m_screen);
	xloadcols();

	/* adjust fixed window geometry */
	twin.setWinExtent(tsize);
	if (m_geometry & XNegative)
		m_left_offset += DisplayWidth(getRawDisplay(), m_screen) - twin.win.width - 2;
	if (m_geometry & YNegative)
		m_top_offset += DisplayHeight(getRawDisplay(), m_screen) - twin.win.height - 2;

	/* Events */
	m_win_attrs.background_pixel = m_draw_ctx.col[config::DEFAULTBG].pixel;
	m_win_attrs.border_pixel = m_draw_ctx.col[config::DEFAULTBG].pixel;
	m_win_attrs.bit_gravity = NorthWestGravity;
	m_win_attrs.event_mask = FocusChangeMask | KeyPressMask | KeyReleaseMask
		| ExposureMask | VisibilityChangeMask | StructureNotifyMask
		| ButtonMotionMask | ButtonPressMask | ButtonReleaseMask;
	m_win_attrs.colormap = m_color_map;

	std::optional<xpp::XWindow> parent;
	if (m_cmdline->embed_window.isSet()) {
		// use window ID passed on command line as parent
		parent.emplace(xpp::XWindow(m_cmdline->embed_window.getValue()));
	}

	if (!parent) {
		// either not embedded or the command line parsing failed
		parent.emplace(xpp::RootWin(*m_display, m_screen));
	}

	m_window = m_display->createWindow(
		xpp::WindowSpec{m_left_offset, m_top_offset,
			static_cast<unsigned int>(twin.win.width),
			static_cast<unsigned int>(twin.win.height)},
		0,
		/*clazz = */InputOutput,
		&(*parent),
		m_display->getDefaultDepth(m_screen),
		m_visual,
		CWBackPixel | CWBorderPixel | CWBitGravity | CWEventMask | CWColormap,
		&m_win_attrs
	);

	XGCValues gcvalues = {};
	gcvalues.graphics_exposures = False;
	m_draw_ctx.gc = XCreateGC(*m_display, parent->id(), GCGraphicsExposures, &gcvalues);
	m_draw_buf = XCreatePixmap(*m_display, m_window, twin.win.width, twin.win.height, m_display->getDefaultDepth(m_screen));
	XSetForeground(*m_display, m_draw_ctx.gc, m_draw_ctx.col[config::DEFAULTBG].pixel);
	XFillRectangle(*m_display, m_draw_buf, m_draw_ctx.gc, 0, 0, twin.win.width, twin.win.height);

	/* font spec buffer */
	m_font_specs.resize(tsize.cols);

	/* Xft rendering context */
	m_font_draw = XftDrawCreate(*m_display, m_draw_buf, m_visual, m_color_map);

	/* input methods */
	ximOpen();

	/* white cursor, black outline */
	Cursor cursor = XCreateFontCursor(*m_display, config::MOUSESHAPE);
	XDefineCursor(*m_display, m_window, cursor);

	XColor xmousefg, xmousebg;

	if (XParseColor(*m_display, m_color_map, getColorName(config::MOUSEFG), &xmousefg) == 0) {
		xmousefg.red   = 0xffff;
		xmousefg.green = 0xffff;
		xmousefg.blue  = 0xffff;
	}

	if (XParseColor(*m_display, m_color_map, getColorName(config::MOUSEBG), &xmousebg) == 0) {
		xmousebg.red   = 0x0000;
		xmousebg.green = 0x0000;
		xmousebg.blue  = 0x0000;
	}

	XRecolorCursor(*m_display, cursor, &xmousefg, &xmousebg);

	m_wmdeletewin = getAtom("WM_DELETE_WINDOW");
	m_netwmname = getAtom("_NET_WM_NAME");
	m_netwmiconname = getAtom("_NET_WM_ICON_NAME");
	XSetWMProtocols(*m_display, m_window, &m_wmdeletewin, 1);

	auto netwmpid = getAtom("_NET_WM_PID");
	auto thispid = cosmos::g_process.getPid();
	XChangeProperty(*m_display, m_window, netwmpid, XA_CARDINAL, 32,
			PropModeReplace, (uchar *)&thispid, 1);

	twin.mode = WinModeMask(WinMode::NUMLOCK);
	xsettitle(nullptr);
	setHints();
	XMapWindow(*m_display, m_window);
	XSync(*m_display, False);

	m_xsel.init();

	if (getenv("NST_XSYNC") != nullptr) {
		::XSynchronize(*m_display, true);
	}
}

std::tuple<Font*, FRC> X11::getFontForMode(const Glyph::AttrBitMask &mode) {
	if (mode.allOf({Attr::ITALIC, Attr::BOLD})) {
		return std::make_tuple(&m_draw_ctx.ibfont, FRC::ITALICBOLD);
	} else if (mode[Attr::ITALIC]) {
		return std::make_tuple(&m_draw_ctx.ifont, FRC::ITALIC);
	} else if (mode[Attr::BOLD]) {
		return std::make_tuple(&m_draw_ctx.bfont, FRC::BOLD);
	} else {
		return std::make_tuple(&m_draw_ctx.font, FRC::NORMAL);
	}
}

size_t X11::makeGlyphFontSpecs(XftGlyphFontSpec *specs, const Glyph *glyphs, size_t len, int x, int y) {
	const auto pos = twin.getDrawPos(CharPos{x,y});
	Font *fnt = &m_draw_ctx.font;
	FRC frcflags = FRC::NORMAL;
	int runewidth = twin.chr.width;
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
			std::tie(fnt, frcflags) = getFontForMode(mode);
			runewidth = twin.chr.width * (mode[Attr::WIDE] ? 2 : 1);
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
	if (twin.mode[WinMode::REVERSE]) {
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

	if (base.mode[Attr::BLINK] && twin.mode[WinMode::BLINK])
		fg = bg;
	else if (base.mode[Attr::INVISIBLE])
		fg = bg;

	/* Intelligent cleaning up of the borders. */
	auto pos = twin.getDrawPos({x, y});
	int width = charlen * twin.chr.width;

	if (x == 0) {
		const auto pos1 = DrawPos{0, y ? pos.y : 0};
		const auto pos2 = DrawPos{config::BORDERPX, pos.y + twin.chr.height + 
			((pos.y + twin.chr.height >= config::BORDERPX + twin.tty.height) ? twin.win.height : 0)};
		clearRect(pos1, pos2);
	}

	if (pos.x + width >= config::BORDERPX + twin.tty.width) {
		const auto pos1 = DrawPos{pos.x + width, y ? pos.y : 0};
		const auto pos2 = DrawPos{twin.win.width,
			(pos.y + twin.chr.height >= config::BORDERPX + twin.tty.height) ? twin.win.height : (pos.y + twin.chr.height)};
		clearRect(pos1, pos2);
	}

	if (y == 0)
		clearRect(DrawPos{pos.x, 0}, DrawPos{pos.x + width, config::BORDERPX});

	if (pos.y + twin.chr.height >= config::BORDERPX + twin.tty.height)
		clearRect(DrawPos{pos.x, pos.y + twin.chr.height}, DrawPos{pos.x + width, twin.win.height});

	/* Clean up the region we want to draw to. */
	XftDrawRect(m_font_draw, bg, pos.x, pos.y, width, twin.chr.height);

	/* Set the clip region because Xft is sometimes dirty. */
	XRectangle r;
	r.x = 0;
	r.y = 0;
	r.height = twin.chr.height;
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

void xdrawcursor(int cx, int cy, Glyph g, int ox, int oy, Glyph og) {
	return x11.drawCursor(cx, cy, g, ox, oy, og);
}

void X11::drawCursor(int cx, int cy, Glyph g, int ox, int oy, Glyph og) {
	auto &sel = Nst::getSelection();

	/* remove the old cursor */
	if (sel.isSelected(ox, oy))
		og.mode.flip(Attr::REVERSE);
	drawGlyph(og, ox, oy);

	if (twin.mode[WinMode::HIDE])
		return;

	/*
	 * Select the right color for the right mode.
	 */
	g.mode.limit({Attr::BOLD, Attr::ITALIC, Attr::UNDERLINE, Attr::STRUCK, Attr::WIDE});
	Color drawcol;

	if (twin.mode[WinMode::REVERSE]) {
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

	/* draw the new one */
	if (twin.mode[WinMode::FOCUSED]) {
		switch (twin.cursor) {
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
			auto pos = twin.getDrawPos(CharPos{cx,cy+1});
			XftDrawRect(
					m_font_draw, &drawcol,
					pos.x, pos.y - config::CURSORTHICKNESS,
					twin.chr.width, config::CURSORTHICKNESS);
			break;
		}
		case CursorStyle::BLINKING_BAR:
		case CursorStyle::STEADY_BAR: {
			auto pos = twin.getDrawPos(CharPos{cx, cy});
			XftDrawRect(
					m_font_draw, &drawcol,
					pos.x, pos.y,
					config::CURSORTHICKNESS, twin.chr.height);
			break;
		}
		default:
			break;
		}
	} else {
		auto pos = twin.getDrawPos(CharPos{cx, cy});

		XftDrawRect(m_font_draw, &drawcol,
				pos.x,
				pos.y,
				twin.chr.width - 1, 1);
		XftDrawRect(m_font_draw, &drawcol,
				pos.x,
				pos.y,
				1, twin.chr.height - 1);
		XftDrawRect(m_font_draw, &drawcol,
				twin.getNextCol(pos).x - 1,
				pos.y,
				1, twin.chr.height - 1);
		XftDrawRect(m_font_draw, &drawcol,
				pos.x,
				twin.getNextLine(pos).y - 1,
				twin.chr.width, 1);
	}
}

void xseticontitle(const char *p) {
	x11.setIconTitle(p ? std::string(p) : Nst::getInstance().getCmdline().getTitle());
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

void xsettitle(const char *p) {
	x11.setTitle(p ? std::string(p) : Nst::getInstance().getCmdline().getTitle().c_str());
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

void xdrawline(const Line &line, int x1, int y1, int x2) {
	return x11.drawLine(line, x1, y1, x2);
}

void X11::drawLine(const Line &line, int x1, int y1, int x2) {
	Glyph base, newone;
	auto *specs = m_font_specs.data();
	size_t i = 0;
	int ox = 0;
	auto &selection = Nst::getSelection();

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

bool xstartdraw(void) {
	return twin.mode[WinMode::VISIBLE];
}

void xfinishdraw() {
	x11.finishDraw();
}

void X11::finishDraw() {
	XCopyArea(*m_display, m_draw_buf, m_window, m_draw_ctx.gc, 0, 0, twin.win.width, twin.win.height, 0, 0);
	XSetForeground(*m_display, m_draw_ctx.gc,
			m_draw_ctx.col[twin.mode[WinMode::REVERSE] ?
				config::DEFAULTFG : config::DEFAULTBG].pixel);
}

void X11::changeEventMask(long event, bool on_off) {
	modifyBit(m_win_attrs.event_mask, on_off ? 1 : 0, event);
	XChangeWindowAttributes(*m_display, m_window, CWEventMask, &m_win_attrs);
}

void xximspot(const CharPos &chp) {
	x11.getInput().setSpot(chp);
}

void xsetpointermotion(bool set) {
	x11.setPointerMotion(set);
}

void X11::setPointerMotion(bool on_off) {
	modifyBit(m_win_attrs.event_mask, on_off, PointerMotionMask);
	XChangeWindowAttributes(*m_display, m_window, CWEventMask, &m_win_attrs);
}

void xsetmode(bool set, const WinMode &flag) {
	auto mode = twin.mode;
	twin.mode.set(flag, set);
	if (twin.mode[WinMode::REVERSE] != mode[WinMode::REVERSE])
		Nst::getTerm().redraw();
}

void xsetcursor(const CursorStyle &cursor) {
	twin.cursor = cursor;
}

void X11::setUrgency(int add) {
	XWMHints *h = XGetWMHints(*m_display, m_window);

	modifyBit(h->flags, add, XUrgencyHint);
	XSetWMHints(*m_display, m_window, h);
	XFree(h);
}

void xbell(void) {
	if (!(twin.mode[WinMode::FOCUSED]))
		x11.setUrgency(1);
	if (config::BELLVOLUME)
		XkbBell(x11.getDisplay(), x11.getWindow(), config::BELLVOLUME, (Atom)NULL);
}

// TODO: move into XEventHandler.cxx once get*Shortcuts() becomes available
// outside of this unit
XEventHandler::XEventHandler(Nst &nst, TermWindow &twin) :
	m_nst(nst),
	m_twin(twin),
	m_x11(nst.getX11()),
	m_xsel(m_x11.getXSelection()),
	m_mouse_shortcuts(config::getMouseShortcuts(nst)),
	m_kbd_shortcuts(config::getKbdShortcuts(nst))
{}

void Nst::waitForWindowMapping() {
	xpp::Event ev;
	auto win = m_term_win.win;

	/* Waiting for window mapping */
	do {
		m_x11.getDisplay().getNextEvent(ev);
		/*
		 * This XFilterEvent call is required because of XOpenIM. It
		 * does filter out the key event and some client message for
		 * the input method too.
		 */
		if (ev.filterEvent())
			continue;

		if (ev.isConfigureNotify()) {
			const auto &configure = ev.toConfigureNotify();
			win.width = configure.width;
			win.height = configure.height;
		}
	} while (!ev.isMapNotify());

	Nst::getInstance().resizeConsole(win);
}

void Nst::applyCmdline() {
	if (m_cmdline.use_alt_screen.isSet()) {
		m_term.setAllowAltScreen(m_cmdline.use_alt_screen.getValue());
	} else {
		m_term.setAllowAltScreen(config::ALLOWALTSCREEN);
	}

	if (m_cmdline.fixed_geometry.isSet()) {
		m_x11.setFixedGeometry(true);
	}

	if (m_cmdline.window_geometry.isSet()) {
		x11.setGeometry(m_cmdline.window_geometry.getValue());
	}
}

Nst::Nst() :
		m_term_win(twin),
		m_x11(x11),
		m_tty(&m_term),
		m_term(m_tty, m_selection),
		m_selection(m_term),
		m_event_handler(*this, twin) {
	if (the_instance) {
		cosmos_throw (cosmos::UsageError("more than once Nst instances alive"));
	}
	the_instance = this;
	xsetcursor(config::CURSORSHAPE);
}

void Nst::run(int argc, const char **argv) {
	m_cmdline.parse(argc, argv);
	tsize.cols = std::max(tsize.cols, 1);
	tsize.rows = std::max(tsize.rows, 1);
	m_term.init(tsize.cols, tsize.rows);
	applyCmdline();

	setlocale(LC_CTYPE, "");
	XSetLocaleModifiers("");
	x11.init();
	m_event_handler.init();
	setEnv();
	mainLoop();
}

void Nst::setEnv() {
	::setenv("WINDOWID", std::to_string(x11.getWindow()).c_str(), 1);
}


void Nst::mainLoop() {
	auto ttyfd = m_tty.create(m_cmdline);

	auto &display = x11.getDisplay();
	auto childfd = m_tty.getChildFD();
	auto xfd = display.getConnectionNumber();

	cosmos::Poller poller;
	poller.create();
	for (auto fd: {ttyfd, xfd, childfd}) {
		poller.addFD(fd, cosmos::Poller::MonitorMask({cosmos::Poller::MonitorSetting::INPUT}));
	}

	xpp::Event ev;
	bool drawing = false;
	cosmos::MonotonicStopWatch draw_watch, blink_watch(cosmos::MonotonicStopWatch::InitialMark(true));
	std::chrono::milliseconds timeout(-1);

	waitForWindowMapping();

	while (true) {
		if (display.hasPendingEvents())
			timeout = std::chrono::milliseconds(0);  /* existing events might not set xfd */

		auto events = poller.wait(timeout.count() >= 0 ?
				std::optional<std::chrono::milliseconds>(timeout) :
				std::nullopt);

		bool draw_event = false;

		for (const auto &event: events) {
			if (event.fd() == childfd)
				m_tty.sigChildEvent();
			else if (event.fd() == ttyfd) {
				m_tty.read();
				draw_event = true;
			}
		}

		while (display.hasPendingEvents()) {
			draw_event = true;
			display.getNextEvent(ev);
			if (ev.filterEvent())
				continue;
			m_event_handler.process(ev);
		}

		/*
		 * To reduce flicker and tearing, when new content or event
		 * triggers drawing, we first wait a bit to ensure we got
		 * everything, and if nothing new arrives - we draw.
		 * We start with trying to wait minlatency ms. If more content
		 * arrives sooner, we retry with shorter and shorter periods,
		 * and eventually draw even without idle after MAXLATENCY ms.
		 * Typically this results in low latency while interacting,
		 * maximum latency intervals during `cat huge.txt`, and perfect
		 * sync with periodic updates from animations/key-repeats/etc.
		 */
		if (draw_event) {
			if (!drawing) {
				draw_watch.mark();
				drawing = true;
			}

			const auto diff = draw_watch.elapsed();
			timeout = (config::MAXLATENCY - diff) / config::MAXLATENCY * config::MINLATENCY;

			if (timeout.count() > 0)
				continue;  /* we have time, try to find idle */
		}

		/* idle detected or maxlatency exhausted -> draw */
		timeout = std::chrono::milliseconds(-1);
		if (config::BLINKTIMEOUT.count() > 0 && m_term.testAttrSet(Attr::BLINK)) {
			timeout = config::BLINKTIMEOUT - blink_watch.elapsed();
			if (timeout.count() <= 0) {
				if (-timeout.count() > config::BLINKTIMEOUT.count()) /* start visible */
					m_term_win.mode.set(WinMode::BLINK);
				m_term_win.mode.flip(WinMode::BLINK);
				m_term.setDirtyByAttr(Attr::BLINK);
				blink_watch.mark();
				timeout = config::BLINKTIMEOUT;
			}
		}

		m_term.draw();
		display.flush();
		drawing = false;
	}
}

} // end ns nst

int main(int argc, const char **argv) {
	try {
		nst::Nst nst;
		xpp::Init xpp;
		nst.run(argc, argv);
	} catch (const std::exception &ex) {
		std::cerr << ex.what() << std::endl;
		return EXIT_FAILURE;
	}

	return 0;
}
