// C++
#include <algorithm>

// X11
#include <X11/Xatom.h>
#include <X11/Xlib.h>
#include <X11/keysym.h>
#include <X11/XKBlib.h>

// cosmos
#include "cosmos/algs.hxx"
#include "cosmos/types.hxx"
#include "cosmos/error/ApiError.hxx"
#include "cosmos/error/RuntimeError.hxx"
#include "cosmos/formatting.hxx"
#include "cosmos/proc/process.hxx"

// X++
#include "X++/atoms.hxx"
#include "X++/helpers.hxx"
#include "X++/Event.hxx"
#include "X++/RootWin.hxx"
#include "X++/XDisplay.hxx"
#include "X++/Xpp.hxx"

// nst
#include "codecs.hxx"
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
inline void modifyBit(T &mask, const bool set, const V bit) {
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
			xpp::to_drawable(parent),
			xpp::GcOptMask{xpp::GcOpts::GraphicsExposures},
			gcvalues);
}

void DrawingContext::setForeground(const FontColor &color) {
	XSetForeground(*m_display, getRawGC(), color.pixel);
}

void DrawingContext::fillRectangle(const DrawPos &pos, const Extent &ext) {
	XFillRectangle(*m_display, xpp::raw_pixmap(m_pixmap), getRawGC(), pos.x, pos.y, ext.width, ext.height);
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

X11::X11(Nst &nst) :
		m_nst{nst},
		m_input{m_window},
		m_cmdline{nst.cmdline()},
		m_display{xpp::display},
		m_screen{m_display.defaultScreen()},
		m_visual{m_display.defaultVisual(m_screen)},
		m_xsel{nst} {
	setCursorStyle(config::CURSORSHAPE);
}

X11::~X11() {
	if (m_font_draw) {
		XftDrawDestroy(m_font_draw);
	}
	m_draw_ctx.freeGC();
}

void X11::copyToClipboard() {
	m_xsel.copyPrimaryToClipboard();
}

void X11::pasteClipboard() {
	const auto &clipboard = xpp::atoms::clipboard;

	m_window.convertSelection(clipboard, m_xsel.targetFormat(), clipboard);
}

void X11::pasteSelection() {
	const auto &primary = xpp::atoms::primary_selection;
	m_window.convertSelection(primary, m_xsel.targetFormat(), primary);
}

void X11::toggleNumlock() {
	m_twin.flipFlag(WinMode::NUMLOCK);
}

void X11::zoomFont(float val) {
	m_font_manager.zoom(static_cast<double>(val));
	m_twin.setCharSize(m_font_manager.normalFont());
	m_nst.resizeConsole();
	m_nst.term().redraw();
	setHints();
}

void X11::resetFont() {
	m_font_manager.resetZoom();
}

void X11::allocPixmap() {
	if (m_pixmap != xpp::PixMapID::INVALID) {
		m_display.freePixmap(m_pixmap);
	}
	m_pixmap = m_display.createPixmap(
		m_window,
		m_twin.winExtent()
	);

	if (m_font_draw) {
		XftDrawChange(m_font_draw, xpp::raw_pixmap(m_pixmap));
	} else {
		/* Xft rendering context */
		m_font_draw = XftDrawCreate(m_display, xpp::raw_pixmap(m_pixmap), m_visual, xpp::raw_cmap(m_color_map));
	}

	m_draw_ctx.setPixmap(m_pixmap);
}

void X11::resize(const TermSize &dim) {

	m_twin.setTermDim(dim);
	allocPixmap();
	const auto &win = m_twin.winExtent();
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
			return XftColorAllocValue(m_display, m_visual,
			                          xpp::raw_cmap(m_color_map), &color, ncolor);
		} else {
			auto cname = config::get_color_name(i);
			name = cname.empty() ? nullptr : cname.data();
		}
	}

	return XftColorAllocName(m_display, m_visual, xpp::raw_cmap(m_color_map), name, ncolor);
}

void X11::loadColors() {
	if (m_colors_loaded) {
		for (auto &c: m_draw_ctx.col) {
			XftColorFree(m_display, m_visual, xpp::raw_cmap(m_color_map), &c);
		}
	} else {
		m_color_map = m_display.defaultColormap(m_screen);
		auto len = std::max(256UL + config::EXTENDED_COLORS.size(), 256UL);
		m_draw_ctx.col.resize(len);
	}

	for (size_t i = 0; i < m_draw_ctx.col.size(); i++) {
		if (!loadColor(i, nullptr, &m_draw_ctx.col[i])) {
			if (auto colorname = config::get_color_name(i); !colorname.empty())
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

	XftColorFree(m_display, m_visual, xpp::raw_cmap(m_color_map), &m_draw_ctx.col[idx]);
	m_draw_ctx.col[idx] = ncolor;

	return true;
}

void X11::clearRect(const DrawPos &pos1, const DrawPos &pos2) {
	const auto colindex = m_twin.activeForegroundColor();
	drawRect(m_draw_ctx.col[colindex], pos1, Extent{pos2.x - pos1.x, pos2.y - pos1.y});
}

void X11::drawRect(const FontColor &col, const DrawPos &start, const Extent &ext) {
	XftDrawRect(m_font_draw, &col, start.x, start.y, ext.width, ext.height);
}

void X11::setHints() {
	// note: the X API breaks constness here, thus use a by-value-copy of
	// the command line arguments
	auto wname = m_cmdline.window_name.getValue();
	auto wclass = m_cmdline.window_class.getValue();
	const auto &chr = m_twin.chrExtent();
	const auto &win = m_twin.winExtent();
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

	if (m_cmdline.fixed_geometry.isSet()) {
		sizeh->flags |= PMaxSize;
		sizeh->min_width = sizeh->max_width = win.width;
		sizeh->min_height = sizeh->max_height = win.height;
	}
	if (m_geometry_mask.anyOf({xpp::GeometrySettings::NegativeX, xpp::GeometrySettings::NegativeY})) {
		sizeh->flags |= USPosition | PWinGravity;
		sizeh->x = m_win_geometry.x;
		sizeh->y = m_win_geometry.y;
		sizeh->win_gravity = gravity();
	}

	XSetWMProperties(m_display, xpp::raw_win(m_window.id()), NULL, NULL, NULL, 0, sizeh.get(), &wm, &clazz);
}

int X11::gravity() {
	using Geometry = xpp::GeometrySettings;
	switch (m_geometry_mask & xpp::GeometrySettingsMask({Geometry::NegativeX, Geometry::NegativeY})) {
	case Geometry::HaveNone:
		return NorthWestGravity;
	case Geometry::NegativeX:
		return NorthEastGravity;
	case Geometry::NegativeY:
		return SouthWestGravity;
	default: // both are negative
		return SouthEastGravity;
	}
}

void X11::setGeometry(const std::string_view str, TermSize &tsize) {
	m_geometry_mask = xpp::parse_geometry(str, m_win_geometry);

	tsize.rows = m_win_geometry.height;
	tsize.cols = m_win_geometry.width;
	m_twin.setWinExtent(tsize);
	const auto &win = m_twin.winExtent();
	if (m_geometry_mask[xpp::GeometrySettings::NegativeX])
		m_win_geometry.x += m_display.displayWidth(m_screen) - win.width - 2;
	if (m_geometry_mask[xpp::GeometrySettings::NegativeY])
		m_win_geometry.y += m_display.displayHeight(m_screen) - win.height - 2;
}

xpp::XWindow X11::parent() const {
	xpp::XWindow ret;

	if (m_cmdline.embed_window.isSet()) {
		// use window ID passed on command line as parent
		ret = xpp::XWindow(xpp::WinID{m_cmdline.embed_window.getValue()});
	}

	if (!ret.valid()) {
		// either not embedded or the command line parsing failed
		ret = xpp::RootWin{m_display, m_screen};
	}

	return ret;
}

void X11::setupCursor() {
	/* white cursor, black outline */
	Cursor cursor = XCreateFontCursor(m_display, config::MOUSE_SHAPE);
	XDefineCursor(m_display, xpp::raw_win(m_window), cursor);

	XColor xmousefg, xmousebg;

	auto parseColor = [this](size_t colnr, XColor &out) {
		auto cname = config::get_color_name(colnr);
		auto res = XParseColor(m_display, xpp::raw_cmap(m_color_map), cname.empty() ? nullptr : cname.data(), &out);
		return res != 0;
	};

	if (!parseColor(config::MOUSE_FG, xmousefg)) {
		xmousefg.red   = 0xffff;
		xmousefg.green = 0xffff;
		xmousefg.blue  = 0xffff;
	}

	if (!parseColor(config::MOUSE_BG, xmousebg)) {
		xmousebg.red   = 0x0000;
		xmousebg.green = 0x0000;
		xmousebg.blue  = 0x0000;
	}

	XRecolorCursor(m_display, cursor, &xmousefg, &xmousebg);
}

void X11::init() {
	const auto &fontspec = m_cmdline.font.getValue();

	m_font_manager.setFontSpec(fontspec);

	if (!m_font_manager.loadFonts()) {
		cosmos_throw (cosmos::RuntimeError(cosmos::sprintf("Failed to open font %s", fontspec.c_str())));
	}

	/* Setting character width and height. */
	m_twin.setCharSize(m_font_manager.normalFont());

	/* colors */
	loadColors();

	TermSize tsize{config::COLS, config::ROWS};
	tsize.normalize();

	/* adjust fixed window geometry */
	if (m_cmdline.window_geometry.isSet()) {
		setGeometry(m_cmdline.window_geometry.getValue(), tsize);
	} else {
		m_twin.setWinExtent(tsize);
	}

	/* font spec buffer */
	m_font_specs.resize(tsize.cols);

	/* Events */
	const auto &bgcolor = m_draw_ctx.col[config::DEFAULT_BG];
	m_win_attrs.background_pixel = bgcolor.pixel;
	m_win_attrs.border_pixel = bgcolor.pixel;
	m_win_attrs.bit_gravity = NorthWestGravity;
	m_win_attrs.event_mask = FocusChangeMask | KeyPressMask | KeyReleaseMask
		| ExposureMask | VisibilityChangeMask | StructureNotifyMask
		| ButtonMotionMask | ButtonPressMask | ButtonReleaseMask;
	m_win_attrs.colormap = xpp::raw_cmap(m_color_map);

	auto parent = this->parent();
	const auto &win = m_twin.winExtent();

	m_win_geometry.width = win.width;
	m_win_geometry.height = win.height;

	m_window = m_display.createWindow(
		m_win_geometry,
		/*border_width=*/0,
		/*clazz = */InputOutput,
		&parent,
		m_display.defaultDepth(m_screen),
		m_visual,
		CWBackPixel | CWBorderPixel | CWBitGravity | CWEventMask | CWColormap,
		&m_win_attrs
	);

	m_draw_ctx.createGC(m_display, parent);
	allocPixmap();
	m_draw_ctx.setForeground(bgcolor);
	m_draw_ctx.fillRectangle(DrawPos{0,0}, win);

	/* input methods */
	m_input.tryOpen();

	setupCursor();

	m_window.setProtocols(xpp::AtomIDVector{xpp::atoms::icccm_wm_delete_window});

	static_assert(sizeof(cosmos::ProcessID) == 4, "NET_WM_PID requires a 32-bit pid type");
	xpp::Property<int> pid_prop(
		cosmos::to_integral(cosmos::proc::cached_pids.own_pid)
	);
	m_window.setProperty(xpp::atoms::ewmh_window_pid, pid_prop);

	setDefaultTitle();
	setHints();
	m_display.mapWindow(m_window);
	m_display.sync();

	m_xsel.init();

	if (m_cmdline.useXSync()) {
		m_display.setSynchronized(true);
	}
}


size_t X11::makeGlyphFontSpecs(XftGlyphFontSpec *specs, const Glyph *glyphs, size_t len, const CharPos &loc) {
	Font *fnt = &m_font_manager.normalFont();
	const auto chr = m_twin.chrExtent();
	auto runewidth = chr.width;
	size_t numspecs = 0;
	Glyph::AttrBitMask prevmode(Glyph::AttrBitMask::all);
	const auto start = m_twin.toDrawPos(loc);
	DrawPos cur{start.atBelow(fnt->ascent())};

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
			fnt = m_font_manager.fontForMode(mode);
			runewidth = chr.width * (mode[Attr::WIDE] ? 2 : 1);
			cur.y = start.y + fnt->ascent();
		}

		auto [xftfont, glyphidx] = m_font_manager.lookupFontEntry(rune, *fnt);

		auto &spec = specs[numspecs++];
		spec.font = xftfont;
		spec.glyph = glyphidx;
		spec.x = (short)cur.x;
		spec.y = (short)cur.y;
		cur.moveRight(runewidth);
	}

	return numspecs;
}

void X11::glyphColors(const Glyph base, FontColor &fg, FontColor &bg) {

	auto assignBaseColor = [this](FontColor &out, const Glyph::color_t col) {
		if (Glyph::isTrueColor(col)) {
			RenderColor tmp{col};
			XftColorAllocValue(m_display, m_visual, xpp::raw_cmap(m_color_map), &tmp, &out);
		} else {
			// col is a base index < 16
			out = m_draw_ctx.col[col];
		}
	};

	auto invertColor = [this](FontColor &c) {
		c.invert();
		RenderColor tmp(c);
		XftColorAllocValue(m_display, m_visual, xpp::raw_cmap(m_color_map), &tmp, &c);
	};

	assignBaseColor(fg, base.fg);
	assignBaseColor(bg, base.bg);

	/* Change basic system colors [0-7] to bright system colors [8-15] */
	if (base.needBrightColor() && base.isBasicColor())
		fg = m_draw_ctx.col[base.toBrightColor()];

	if (m_twin.inReverseMode()) {
		if (fg == m_draw_ctx.defaultFG()) {
			fg = m_draw_ctx.defaultBG();
		} else {
			invertColor(fg);
		}

		if (bg == m_draw_ctx.defaultBG()) {
			bg = m_draw_ctx.defaultFG();
		} else {
			invertColor(bg);
		}
	}

	if (base.needFaintColor()) {
		auto faint = fg.faint();
		RenderColor tmp(faint);
		XftColorAllocValue(m_display, m_visual, xpp::raw_cmap(m_color_map), &tmp, &fg);
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
	m_font_manager.sanitizeColor(base);
	glyphColors(base, fg, bg);

	auto pos = m_twin.toDrawPos(loc);
	const auto &win = m_twin.winExtent();
	const auto &chr = m_twin.chrExtent();
	const auto &tty = m_twin.TTYExtent();
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
		drawRect(fg, pos.atBelow(m_font_manager.ascent() + 1), Extent{textwidth, 1});
	}

	if (base.mode[Attr::STRUCK]) {
		drawRect(fg, pos.atBelow(2 * m_font_manager.ascent() / 3), Extent{textwidth, 1});
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
	if (m_nst.selection().isSelected(pos))
		glyph.mode.flip(Attr::REVERSE);
	drawGlyph(glyph, pos);
}

const FontColor& X11::cursorColor(const CharPos &pos, Glyph &glyph) const {
	const bool is_selected = m_nst.selection().isSelected(pos);

	/*
	 * Select the right color for the right mode.
	 */
	glyph.mode.limit({Attr::BOLD, Attr::ITALIC, Attr::UNDERLINE, Attr::STRUCK, Attr::WIDE});

	if (m_twin.inReverseMode()) {
		glyph.mode.set(Attr::REVERSE);
		glyph.bg = config::DEFAULT_FG;
		if (is_selected) {
			glyph.fg = config::DEFAULT_RCS;
			return m_draw_ctx.col[config::DEFAULT_CS];
		} else {
			glyph.fg = config::DEFAULT_CS;
			return m_draw_ctx.col[config::DEFAULT_RCS];
		}
	} else {
		if (is_selected) {
			glyph.fg = config::DEFAULT_FG;
			glyph.bg = config::DEFAULT_RCS;
		} else {
			glyph.fg = config::DEFAULT_BG;
			glyph.bg = config::DEFAULT_CS;
		}

		return m_draw_ctx.col[glyph.bg];
	}
}

void X11::drawCursor(const CharPos &pos, Glyph glyph) {

	if (m_twin.checkFlag(WinMode::HIDE_CURSOR))
		return;

	auto &drawcol = cursorColor(pos, glyph);
	auto &chr = m_twin.chrExtent();
	constexpr auto CURSOR_THICKNESS = config::CURSOR_THICKNESS;

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
			auto dpos = m_twin.toDrawPos(pos.nextLine());
			dpos.moveUp(CURSOR_THICKNESS);
			drawRect(drawcol, dpos, Extent{chr.width, CURSOR_THICKNESS});
			break;
		}
		case CursorStyle::BLINKING_BAR:
		case CursorStyle::STEADY_BAR: {
			auto dpos = m_twin.toDrawPos(pos);
			drawRect(drawcol, dpos, Extent{CURSOR_THICKNESS, chr.height});
			break;
		}
		default:
			break;
		}
	} else {
		// only draw a non-solid rectangle outline of the cursor, if
		// there's no focus
		auto dpos = m_twin.toDrawPos(pos);
		drawRect(drawcol, dpos, Extent{chr.width - 1, 1});
		drawRect(drawcol, dpos, Extent{1, chr.height - 1});

		auto nextcol = m_twin.nextCol(dpos).atLeft(1);
		drawRect(drawcol, nextcol, Extent{1, chr.height - 1});

		auto nextline = m_twin.nextLine(dpos).atAbove(1);
		drawRect(drawcol, nextline, Extent{chr.width, 1});
	}
}

void X11::setDefaultIconTitle() {
	setIconTitle(m_cmdline.title());
}

void X11::setIconTitle(const std::string &title) {
	xpp::Property<xpp::utf8_string> data{xpp::utf8_string(title)};
	m_window.setProperty(xpp::atoms::wm_icon_name, data);
	m_window.setProperty(xpp::atoms::ewmh_icon_name, data);
}

void X11::setDefaultTitle() {
	setTitle(m_cmdline.title());
}

void X11::setTitle(const std::string &title) {
	xpp::Property<xpp::utf8_string> data{xpp::utf8_string(title)};
	m_window.setProperty(xpp::atoms::icccm_window_name, data);
	m_window.setProperty(xpp::atoms::ewmh_window_name, data);
}

void X11::drawLine(const Line &line, const CharPos &start, const int count) {
	Glyph base, newone;
	auto *specs = m_font_specs.data();
	size_t numcols = 0;
	CharPos curpos{0, start.y};
	auto &selection = m_nst.selection();

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
	auto extent = m_twin.winExtent();
	m_window.copyArea(m_draw_ctx.getGC(), m_pixmap, extent);
	m_draw_ctx.setForeground(m_twin.activeForegroundColor());
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
	const auto prevmode = m_twin.mode();
	m_twin.setFlag(flag, set);
	if (m_twin.checkFlag(WinMode::REVERSE) != prevmode[WinMode::REVERSE])
		m_nst.term().redraw();
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
	if (config::BELL_VOLUME)
		XkbBell(m_display, xpp::raw_win(m_window.id()), config::BELL_VOLUME, (Atom)nullptr);
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
		m_nst.term().reportFocus(in_focus);
	}
}

} // end ns nst
