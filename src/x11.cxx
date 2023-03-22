// X11
#include <X11/Xlib.h>

// cosmos
#include "cosmos/algs.hxx"
#include "cosmos/types.hxx"
#include "cosmos/error/RuntimeError.hxx"
#include "cosmos/formatting.hxx"
#include "cosmos/proc/process.hxx"

// X++
#include "X++/atoms.hxx"
#include "X++/Event.hxx"
#include "X++/helpers.hxx"
#include "X++/keyboard.hxx"
#include "X++/RootWin.hxx"
#include "X++/SizeHints.hxx"
#include "X++/XColor.hxx"
#include "X++/XCursor.hxx"
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

namespace nst {

X11::X11(Nst &nst) :
		m_nst{nst},
		m_cmdline{nst.cmdline()},
		m_input{m_window},
		m_color_manager{m_twin},
		m_display{xpp::display},
		m_xsel{nst} {
	setCursorStyle(config::CURSORSHAPE);
}

X11::~X11() {
	m_font_draw_ctx.destroy();
	m_pixmap.destroy();
	m_graphics_context.reset();
}

void X11::createGraphicsContext(xpp::XWindow &parent) {
	XGCValues gcvalues = {};
	gcvalues.graphics_exposures = False;
	m_graphics_context = xpp::display.createGraphicsContext(
		xpp::to_drawable(parent),
		xpp::GcOptMask{xpp::GcOpts::GraphicsExposures},
		gcvalues
	);
}

void X11::setForeground(const FontColor &color) {
	::XSetForeground(xpp::display, m_graphics_context.get(), color.pixel);
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

void X11::zoomFont(double val) {
	m_font_manager.zoom(val);
	m_twin.setCharSize(m_font_manager.normalFont());
	m_nst.resizeConsole();
	m_nst.term().redraw();
	setSizeHints();
}

void X11::resetFont() {
	m_font_manager.resetZoom();
}

void X11::allocPixmap() {
	m_pixmap = xpp::Pixmap{m_window, m_twin.winExtent()};
	m_font_draw_ctx.setup(m_display, m_pixmap);
}

void X11::resize(const TermSize dim) {

	m_twin.setTermDim(dim);
	allocPixmap();
	clearWindow();
	m_font_specs.reserve(dim.cols);
}

void X11::clearWindow() {
	const auto &win = m_twin.winExtent();
	clearRect(DrawPos{0,0}, DrawPos{win.width, win.height});
}

void X11::clearRect(const DrawPos pos1, const DrawPos pos2) {
	const auto idx = m_twin.activeForegroundColor();
	m_font_draw_ctx.drawRect(m_color_manager.fontColor(idx), pos1, Extent{pos2.x - pos1.x, pos2.y - pos1.y});
}

void X11::setupWinAttrs() {
	/* Events */
	m_win_attrs.background_pixel = m_color_manager.defaultBack().pixel;
	m_win_attrs.border_pixel = m_win_attrs.background_pixel;
	m_win_attrs.setBitGravity(xpp::Gravity::NorthWest);
	using Event = xpp::EventMask;
	m_win_attrs.setEventMask(xpp::EventSelectionMask{
			Event::FocusChange, Event::Exposure,
			Event::KeyPresses, Event::KeyReleases,
			Event::VisibilityChange, Event::StructureNotify,
			Event::ButtonMotion,
			Event::ButtonPresses, Event::ButtonReleases
	});
	m_win_attrs.setColormap(xpp::colormap);
}

void X11::setupWindow(xpp::XWindow &parent) {
	using WinAttr = xpp::WindowAttr;

	m_window = m_display.createWindow(
		m_win_geometry,
		/*border_width=*/0,
		xpp::WindowClass::InOut,
		&parent,
		m_display.defaultDepth(),
		xpp::visual,
		xpp::WindowAttrMask({
			WinAttr::BackPixel, WinAttr::BorderPixel, WinAttr::BitGravity,
			WinAttr::EventMask, WinAttr::Colormap}),
		&m_win_attrs
	);

	xpp::WindowManagerHints wm_hints;
	wm_hints.setWMInputHandling(true);
	m_window.setWMHints(wm_hints);

	xpp::ClassHints winclass{
		m_cmdline.window_name.getValue(),
		m_cmdline.window_class.getValue()
	};
	m_window.setClassHints(winclass);

	m_window.setProtocols(
			xpp::AtomIDVector{xpp::atoms::icccm_wm_delete_window});

	static_assert(sizeof(cosmos::ProcessID) == 4, "NET_WM_PID requires a 32-bit pid type");
	xpp::Property<int> pid_prop(
		cosmos::to_integral(cosmos::proc::cached_pids.own_pid)
	);
	m_window.setProperty(xpp::atoms::ewmh_window_pid, pid_prop);

	setDefaultTitle();
	setSizeHints();
}

void X11::setSizeHints() {
	using Flags = xpp::SizeHints::Flags;
	constexpr auto BORDER_PIXELS = 2 * config::BORDERPX;
	const auto chr = m_twin.chrExtent();
	const auto win = m_twin.winExtent();
	xpp::SizeHints size_hints;
	xpp::SizeHints::Mask mask{
			Flags::ProgSize,
			Flags::ProgResizeIncrements,
			Flags::ProgBaseSize,
			Flags::ProgMinSize
	};

	size_hints.clear();
	size_hints.setDimensions(win.width, win.height);
	size_hints.setIncrements(chr.width, chr.height);
	size_hints.setBaseDimensions(BORDER_PIXELS, BORDER_PIXELS);
	size_hints.setMinDimensions(chr.width + BORDER_PIXELS, chr.height + BORDER_PIXELS);

	if (m_cmdline.fixed_geometry.isSet()) {
		mask.set(Flags::ProgMaxSize);
		size_hints.setMinDimensions(win.width, win.height);
		size_hints.setMaxDimensions(win.width, win.height);
	}

	if (m_geometry_mask.anyOf({xpp::GeometrySettings::NegativeX, xpp::GeometrySettings::NegativeY})) {
		mask.set({Flags::UserPos, Flags::ProgWinGravity});
		size_hints.setPosition(xpp::Coord{m_win_geometry.x, m_win_geometry.y});
		size_hints.setWinGravity(gravity());
	}

	size_hints.setFlags(mask);

	m_window.setWMNormalHints(size_hints);
}

xpp::Gravity X11::gravity() const {
	using Geometry = xpp::GeometrySettings;
	using Gravity = xpp::Gravity;

	switch (m_geometry_mask & xpp::GeometrySettingsMask({Geometry::NegativeX, Geometry::NegativeY})) {
	case Geometry::HaveNone:
		return Gravity::NorthWest;
	case Geometry::NegativeX:
		return Gravity::NorthEast;
	case Geometry::NegativeY:
		return Gravity::SouthWest;
	default: // both are negative
		return Gravity::SouthEast;
	}
}

void X11::setGeometry(const std::string_view str, TermSize &tsize) {
	m_geometry_mask = xpp::parse_geometry(str, m_win_geometry);

	tsize.rows = m_win_geometry.height;
	tsize.cols = m_win_geometry.width;
	m_twin.setWinExtent(tsize);
	const auto &win = m_twin.winExtent();
	if (m_geometry_mask[xpp::GeometrySettings::NegativeX])
		m_win_geometry.x += m_display.displayWidth() - win.width - 2;
	if (m_geometry_mask[xpp::GeometrySettings::NegativeY])
		m_win_geometry.y += m_display.displayHeight() - win.height - 2;
}

xpp::XWindow X11::parent() const {
	xpp::XWindow ret;

	if (m_cmdline.embed_window.isSet()) {
		// use window ID passed on command line as parent
		ret = xpp::XWindow(xpp::WinID{m_cmdline.embed_window.getValue()});
	}

	if (!ret.valid()) {
		// either not embedded or the command line parsing failed
		ret = xpp::RootWin{m_display, xpp::screen};
	}

	return ret;
}

void X11::setupCursor() {
	xpp::XCursor cursor{config::MOUSE_SHAPE};

	xpp::XColor fg, bg;

	auto parseColor = [this](ColorIndex idx, xpp::XColor &out, const unsigned short fallback) {
		auto name = config::get_color_name(idx);
		try {
			m_display.parseColor(out, name);
		} catch (const cosmos::CosmosError &) {
			out.setAll(fallback);
		}
	};

	// white cursor, black outline
	parseColor(config::MOUSE_FG, fg, 0xFFFF);
	parseColor(config::MOUSE_BG, bg, 0x0000);

	cursor.recolorCursor(fg, bg);
	m_window.defineCursor(cursor);
}

void X11::init() {
	const auto &fontspec = m_cmdline.font.getValue();

	m_font_manager.setFontSpec(fontspec);

	if (!m_font_manager.loadFonts()) {
		cosmos_throw (cosmos::RuntimeError(cosmos::sprintf("Failed to open font %s", fontspec.c_str())));
	}

	m_twin.setCharSize(m_font_manager.normalFont());

	m_color_manager.init();

	TermSize tsize{config::COLS, config::ROWS};

	/* adjust fixed window geometry */
	if (m_cmdline.window_geometry.isSet()) {
		setGeometry(m_cmdline.window_geometry.getValue(), tsize);
	} else {
		m_twin.setWinExtent(tsize);
	}

	setupWinAttrs();

	auto parent = this->parent();
	const auto win = m_twin.winExtent();

	m_win_geometry.width = win.width;
	m_win_geometry.height = win.height;

	setupWindow(parent);
	createGraphicsContext(parent);
	resize(tsize);

	/* input methods */
	m_input.tryOpen();

	setupCursor();

	m_display.mapWindow(m_window);
	m_display.sync();

	m_xsel.init();

	if (m_cmdline.useXSync()) {
		m_display.setSynchronized(true);
	}
}

void X11::makeGlyphFontSpecs(const Glyph *glyphs, const size_t count, const CharPos ch_pos) {
	const auto chr = m_twin.chrExtent();
	const auto start_pos = m_twin.toDrawPos(ch_pos);
	Glyph::AttrBitMask prevmode{Glyph::AttrBitMask::all};
	DrawPos cur_pos{start_pos};
	Font *font = nullptr;
	int runewidth = 0;
	XftGlyphFontSpec spec;

	m_font_specs.clear();

	for (size_t i = 0; i < count; i++) {
		const auto &glyph = glyphs[i];

		// Skip dummy wide-character spacing.
		if (glyph.isDummy())
			continue;

		// Determine font for glyph if different from previous glyph.
		if (const auto mode = glyph.mode; prevmode != mode) {
			prevmode = mode;
			font = m_font_manager.fontForMode(mode);
			runewidth = chr.width * glyph.width();
			cur_pos.y = start_pos.y + font->ascent();
		}

		m_font_manager.assignFont(glyph.u, *font, spec);
		spec.x = static_cast<short>(cur_pos.x);
		spec.y = static_cast<short>(cur_pos.y);

		m_font_specs.emplace_back(spec);
		cur_pos.moveRight(runewidth);
	}

	m_next_font_spec = m_font_specs.begin();
}

void X11::cleanupWindowBorders(int textwidth, const CharPos ch_pos, const DrawPos draw_pos) {
	constexpr auto BORDERPX = config::BORDERPX;
	const auto chr = m_twin.chrExtent();
	const auto tty = m_twin.TTYExtent();
	const auto win = m_twin.winExtent();
	const bool reaches_bottom_border =
		draw_pos.y + chr.height >= BORDERPX + tty.height;

	// left border
	if (ch_pos.x == 0) {
		const auto pos1 = DrawPos{0, ch_pos.y ? draw_pos.y : 0};
		const auto pos2 = DrawPos{
			BORDERPX,
			ch_pos.y + chr.height + (reaches_bottom_border ? win.height : 0)
		};
		clearRect(pos1, pos2);
	}

	// right border
	if (draw_pos.x + textwidth >= BORDERPX + tty.width) {
		const auto pos1 = DrawPos{
			draw_pos.x + textwidth,
			ch_pos.y ? draw_pos.y : 0};
		const auto pos2 = DrawPos{
			win.width,
			reaches_bottom_border ? win.height : draw_pos.y + chr.height
		};
		clearRect(pos1, pos2);
	}

	// top border
	if (ch_pos.y == 0) {
		clearRect(
				DrawPos{draw_pos.x, 0},
				DrawPos{draw_pos.x + textwidth, BORDERPX});
	}

	// bottom border
	if (draw_pos.y + chr.height >= BORDERPX + tty.height) {
		clearRect(
				DrawPos{draw_pos.x, draw_pos.y + chr.height},
				DrawPos{draw_pos.x + textwidth, win.height});
	}
}

void X11::drawGlyphFontSpecs(Glyph base, const size_t count, const CharPos ch_pos) {

	const auto pos = m_twin.toDrawPos(ch_pos);
	const auto chr = m_twin.chrExtent();
	const int textwidth = count * base.width() * chr.width;

	// Intelligent cleaning up of the borders.
	cleanupWindowBorders(textwidth, ch_pos, pos);

	m_font_manager.sanitize(base);
	m_color_manager.configureFor(base);

	// Clean up the region we want to draw to.
	m_font_draw_ctx.drawRect(m_color_manager.backColor(), pos, Extent{textwidth, chr.height});

	// Set the clip region because Xft is sometimes dirty.
	m_font_draw_ctx.setClipRectangle(pos, Extent{textwidth, chr.height});

	const auto &front_color = m_color_manager.frontColor();

	// Render the glyphs.
	::XftDrawGlyphFontSpec(m_font_draw_ctx.raw(), &front_color, &(*m_next_font_spec), count);

	// Render underline and strikethrough.
	if (base.isUnderlined()) {
		m_font_draw_ctx.drawRect(front_color, pos.atBelow(m_font_manager.ascent() + 1), Extent{textwidth, 1});
	}

	if (base.isStruck()) {
		m_font_draw_ctx.drawRect(front_color, pos.atBelow(2 * m_font_manager.ascent() / 3), Extent{textwidth, 1});
	}

	// Reset clip to none.
	m_font_draw_ctx.resetClip();

	m_next_font_spec += count;
}

void X11::drawGlyph(Glyph g, const CharPos pos) {
	makeGlyphFontSpecs(&g, 1, pos);
	drawGlyphFontSpecs(g, 1, pos);
}

void X11::drawGlyphs(Line::const_iterator it, const Line::const_iterator end, CharPos start_pos) {
	// NOTE: in C++20 we can use a std::span here to pass in a sub-vector
	// instead of the more complicated iterator range.
	const auto &selection = m_nst.selection();
	Glyph base = *it;
	size_t num_specs = 0;
	CharPos cur_pos{start_pos};

	makeGlyphFontSpecs(&(*it), end - it, start_pos);

	size_t specs_left = m_font_specs.end() - m_next_font_spec;

	// Collect series of Glyphs that share the same drawing features and
	// feed them into drawGlyphFontSpecs until we're done with the given
	// range.

	for (
			Glyph glyph = *it;
			it < end && num_specs < specs_left;
			glyph = *++it, cur_pos.moveRight()) {

		if (glyph.isDummy()) {
			continue;
		} else if (selection.isSelected(cur_pos)) {
			glyph.mode.flip(Attr::REVERSE);
		}

		// a change in drawing features occured, draw the series we collected so far
		if (num_specs != 0 && base.featuresDiffer(glyph)) {
			drawGlyphFontSpecs(base, num_specs, start_pos);
			specs_left = m_font_specs.end() - m_next_font_spec;
			num_specs = 0;
			// a new series started, remember its properties
			start_pos = cur_pos;
			base = glyph;
		}

		num_specs++;
	}

	if (num_specs != 0) {
		drawGlyphFontSpecs(base, num_specs, start_pos);
	}
}

void X11::clearCursor(const CharPos pos, Glyph glyph) {
	/* remove the old cursor */
	if (m_nst.selection().isSelected(pos))
		glyph.mode.flip(Attr::REVERSE);
	drawGlyph(glyph, pos);
}

void X11::drawCursor(const CharPos pos, Glyph glyph) {

	if (m_twin.checkFlag(WinMode::HIDE_CURSOR))
		return;

	auto &drawcol = m_color_manager.cursorColor(m_nst.selection().isSelected(pos), glyph);
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
			m_font_draw_ctx.drawRect(drawcol, dpos, Extent{chr.width, CURSOR_THICKNESS});
			break;
		}
		case CursorStyle::BLINKING_BAR:
		case CursorStyle::STEADY_BAR: {
			auto dpos = m_twin.toDrawPos(pos);
			m_font_draw_ctx.drawRect(drawcol, dpos, Extent{CURSOR_THICKNESS, chr.height});
			break;
		}
		default:
			break;
		}
	} else {
		// only draw a non-solid rectangle outline of the cursor, if
		// there's no focus
		auto dpos = m_twin.toDrawPos(pos);
		m_font_draw_ctx.drawRect(drawcol, dpos, Extent{chr.width - 1, 1});
		m_font_draw_ctx.drawRect(drawcol, dpos, Extent{1, chr.height - 1});

		auto nextcol = m_twin.nextCol(dpos).atLeft(1);
		m_font_draw_ctx.drawRect(drawcol, nextcol, Extent{1, chr.height - 1});

		auto nextline = m_twin.nextLine(dpos).atAbove(1);
		m_font_draw_ctx.drawRect(drawcol, nextline, Extent{chr.width, 1});
	}
}

void X11::setDefaultIconTitle() {
	setIconTitle(m_cmdline.title());
}

void X11::setIconTitle(const std::string_view title) {
	xpp::Property<xpp::utf8_string> data{xpp::utf8_string(title)};
	m_window.setProperty(xpp::atoms::wm_icon_name, data);
	m_window.setProperty(xpp::atoms::ewmh_icon_name, data);
}

void X11::setDefaultTitle() {
	setTitle(m_cmdline.title());
}

void X11::setTitle(const std::string_view title) {
	xpp::Property<xpp::utf8_string> data{xpp::utf8_string(title)};
	m_window.setProperty(xpp::atoms::icccm_window_name, data);
	m_window.setProperty(xpp::atoms::ewmh_window_name, data);
}

void X11::finishDraw() {
	auto extent = m_twin.winExtent();
	m_window.copyArea(m_graphics_context, m_pixmap, extent);
	setForeground(m_color_manager.fontColor(m_twin.activeForegroundColor()));
}

void X11::changeEventMask(const xpp::EventMask event, bool on_off) {
	m_win_attrs.changeEventMask(event, on_off);
	m_window.setWindowAttrs(m_win_attrs, xpp::WindowAttrMask{xpp::WindowAttr::EventMask});
}

void X11::setMode(const WinMode flag, const bool set) {
	const auto prevmode = m_twin.mode();
	m_twin.setFlag(flag, set);
	if (m_twin.checkFlag(WinMode::REVERSE) != prevmode[WinMode::REVERSE])
		m_nst.term().redraw();
}

void X11::setCursorStyle(const CursorStyle cursor) {
	m_twin.setCursorStyle(cursor);
}

void X11::setUrgency(const bool have_urgency) {
	// should never be nullptr, since we've set hints initially
	auto hints = m_window.getWMHints();

	if (!hints)
		return;

	hints->changeFlag(xpp::WindowManagerHints::Flags::Urgency, have_urgency);

	m_window.setWMHints(*hints);
}

void X11::ringBell() {
	if (!(m_twin.checkFlag(WinMode::FOCUSED)))
		setUrgency(true);
	if (config::BELL_VOLUME != xpp::BellVolume::NONE) {
		xpp::ring_bell(m_window, config::BELL_VOLUME);
	}
}

void X11::embeddedFocusChange(const bool in_focus) {
	// called when we run embedded in another window and the focus changes
	if (in_focus) {
		m_twin.setFlag(WinMode::FOCUSED);
		setUrgency(false);
	} else {
		m_twin.resetFlag(WinMode::FOCUSED);
	}
}

void X11::focusChange(const bool in_focus) {
	if (in_focus) {
		m_input.setFocus();
		m_twin.setFlag(WinMode::FOCUSED);
		setUrgency(false);
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
