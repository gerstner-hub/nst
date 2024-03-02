// cosmos
#include "cosmos/error/RuntimeError.hxx"
#include "cosmos/formatting.hxx"
#include "cosmos/proc/process.hxx"
#include "cosmos/utils.hxx"

// xpp
#include "xpp/atoms.hxx"
#include "xpp/event/ClientMessageEvent.hxx"
#include "xpp/Event.hxx"
#include "xpp/helpers.hxx"
#include "xpp/keyboard.hxx"
#include "xpp/Property.hxx"
#include "xpp/RootWin.hxx"
#include "xpp/SizeHints.hxx"
#include "xpp/WindowManagerHints.hxx"
#include "xpp/XColor.hxx"
#include "xpp/XDisplay.hxx"

// nst
#include "nst.hxx"
#include "types.hxx"
#include "WindowSystem.hxx"

namespace nst {

WindowSystem::WindowSystem(Nst &nst) :
		m_nst{nst},
		m_cmdline{nst.cmdline()},
		m_input{m_window},
		m_color_manager{m_twin},
		m_selection{nst},
		m_border_pixels{config::BORDERPX},
		m_cursor_thickness{config::CURSOR_THICKNESS},
		m_display{xpp::display} {
	setCursorStyle(config::CURSORSHAPE);
}

WindowSystem::~WindowSystem() {
	m_font_draw_ctx.destroy();
	m_pixmap.destroy();
	m_graphics_context.destroy();
}

void WindowSystem::createGraphicsContext(const xpp::XWindow &parent) {
	XGCValues gcvalues = {};
	// we don't want to receive exposure events for the context
	gcvalues.graphics_exposures = False;
	m_graphics_context = xpp::GraphicsContext{
		xpp::to_drawable(parent),
		xpp::GcOptMask{xpp::GcOpts::GRAPHICS_EXPOSURES},
		gcvalues
	};
}

void WindowSystem::copyToClipboard() {
	m_selection.copyPrimaryToClipboard();
}

void WindowSystem::pasteClipboard() {
	const auto clipboard = xpp::atoms::clipboard;

	m_window.convertSelection(clipboard, m_selection.targetFormat(), clipboard);
}

void WindowSystem::pasteSelection() {
	const auto primary = xpp::atoms::primary_selection;
	m_window.convertSelection(primary, m_selection.targetFormat(), primary);
}

void WindowSystem::toggleNumlock() {
	m_twin.flipFlag(WinMode::NUMLOCK);
}

void WindowSystem::zoomFont(double val) {
	m_font_manager.zoom(val);
	handleFontUpdate();
}

void WindowSystem::resetFont() {
	m_font_manager.resetZoom();
	handleFontUpdate();
}

void WindowSystem::handleFontUpdate() {
	m_twin.setCharSize(m_font_manager.normalFont());
	m_nst.resizeConsole();
	m_nst.term().redraw();
	setSizeHints();
}

void WindowSystem::allocPixmap() {
	m_pixmap = xpp::Pixmap{m_window, m_twin.winExtent()};
	m_font_draw_ctx.setup(m_display, m_pixmap);
}

void WindowSystem::resize(const TermSize dim) {

	m_twin.setTermDim(dim);
	allocPixmap();
	clearWindow();
	m_font_specs.reserve(dim.cols);
}

void WindowSystem::clearWindow() {
	const auto win = m_twin.winExtent();
	clearRect(DrawPos{0,0}, DrawPos{win.width, win.height});
}

void WindowSystem::clearRect(const DrawPos pos1, const DrawPos pos2) {
	const auto idx = m_twin.activeForegroundColor();
	m_font_draw_ctx.drawRect(m_color_manager.fontColor(idx), pos1, Extent{pos2.x - pos1.x, pos2.y - pos1.y});
}

void WindowSystem::setupWinAttrs() {
	m_win_attrs.background_pixel = m_color_manager.defaultBack().pixel;
	m_win_attrs.border_pixel = m_win_attrs.background_pixel;
	m_win_attrs.setBitGravity(xpp::Gravity::NORTH_WEST);
	using Event = xpp::EventMask;
	m_win_attrs.setEventMask(xpp::EventSelectionMask{
			Event::FOCUS_CHANGE, Event::EXPOSURE,
			Event::KEY_PRESSES, Event::KEY_RELEASES,
			Event::VISIBILITY_CHANGE, Event::STRUCTURE_NOTIFY,
			Event::BUTTON_MOTION,
			Event::BUTTON_PRESSES, Event::BUTTON_RELEASES
	});
	m_win_attrs.setColormap(xpp::colormap);
}

void WindowSystem::setupWindow(const xpp::XWindow &parent) {
	using WinAttr = xpp::WindowAttr;

	m_window = m_display.createWindow(
		m_win_geometry,
		/*border_width=*/0,
		xpp::WindowClass::INPUT_OUTPUT,
		&parent,
		m_display.defaultDepth(),
		xpp::visual,
		xpp::WindowAttrMask({
			WinAttr::BACK_PIXEL, WinAttr::BORDER_PIXEL, WinAttr::BIT_GRAVITY,
			WinAttr::EVENT_MASK, WinAttr::COLORMAP}),
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

	m_window.setProtocols(xpp::AtomIDVector{xpp::atoms::icccm_wm_delete_window});

	static_assert(sizeof(cosmos::ProcessID) == 4, "NET_WM_PID requires a 32-bit pid type");
	xpp::Property<int> pid_prop(
		cosmos::to_integral(cosmos::proc::cached_pids.own_pid)
	);
	m_window.setProperty(xpp::atoms::ewmh_window_pid, pid_prop);

	setDefaultTitle();
	setSizeHints();
}

void WindowSystem::setSizeHints() {
	using Flags = xpp::SizeHints::Flags;
	const auto BORDER_PIXELS = 2 * m_border_pixels;
	const auto chr = m_twin.chrExtent();
	const auto win = m_twin.winExtent();
	xpp::SizeHints size_hints;
	xpp::SizeHints::Mask mask{
			Flags::PROG_SIZE,
			Flags::PROG_RESIZE_INCREMENTS,
			Flags::PROG_BASE_SIZE,
			Flags::PROG_MIN_SIZE
	};

	size_hints.clear();
	size_hints.setDimensions(win.width, win.height);
	size_hints.setIncrements(chr.width, chr.height);
	size_hints.setBaseDimensions(BORDER_PIXELS, BORDER_PIXELS);
	size_hints.setMinDimensions(chr.width + BORDER_PIXELS, chr.height + BORDER_PIXELS);

	if (m_cmdline.fixed_geometry.isSet()) {
		mask.set(Flags::PROG_MAX_SIZE);
		size_hints.setMinDimensions(win.width, win.height);
		size_hints.setMaxDimensions(win.width, win.height);
	}

	if (m_geometry_mask.anyOf({xpp::GeometrySettings::X_NEGATIVE, xpp::GeometrySettings::Y_NEGATIVE})) {
		mask.set({Flags::USER_POS, Flags::PROG_WIN_GRAVITY});
		size_hints.setPosition(xpp::Coord{m_win_geometry.x, m_win_geometry.y});
		size_hints.setWinGravity(gravity());
	}

	size_hints.setFlags(mask);

	m_window.setWMNormalHints(size_hints);
}

xpp::Gravity WindowSystem::gravity() const {
	using Geometry = xpp::GeometrySettings;
	using Gravity = xpp::Gravity;

	switch (m_geometry_mask & xpp::GeometrySettingsMask({Geometry::X_NEGATIVE, Geometry::Y_NEGATIVE})) {
	case Geometry::HAVE_NONE:
		return Gravity::NORTH_WEST;
	case Geometry::X_NEGATIVE:
		return Gravity::NORTH_EAST;
	case Geometry::Y_NEGATIVE:
		return Gravity::SOUTH_WEST;
	default: // both are negative
		return Gravity::SOUTH_EAST;
	}
}

void WindowSystem::setGeometry(const cosmos::SysString str, TermSize &tsize) {
	m_geometry_mask = xpp::parse_geometry(str, m_win_geometry);

	tsize.rows = m_win_geometry.height;
	tsize.cols = m_win_geometry.width;
	m_twin.setWinExtent(tsize);
	const auto &win = m_twin.winExtent();
	if (m_geometry_mask[xpp::GeometrySettings::X_NEGATIVE])
		m_win_geometry.x += m_display.displayWidth() - win.width - 2;
	if (m_geometry_mask[xpp::GeometrySettings::Y_NEGATIVE])
		m_win_geometry.y += m_display.displayHeight() - win.height - 2;
}

xpp::XWindow WindowSystem::parent() const {
	xpp::XWindow ret;

	if (m_cmdline.embed_window.isSet()) {
		// use window ID passed on command line as parent.
		ret = xpp::XWindow(xpp::WinID{m_cmdline.embed_window.getValue()});
	}

	if (!ret.valid()) {
		// either not embedded or the parsing failed, use the root window.
		ret = xpp::RootWin{m_display, xpp::screen};
	}

	return ret;
}

void WindowSystem::init() {

	TermSize tsize{config::COLS, config::ROWS};
	applyConfig(tsize);

	m_color_manager.init();


	// adjust fixed window geometry
	if (m_cmdline.window_geometry.isSet()) {
		setGeometry(m_cmdline.window_geometry.getValue(), tsize);
	} else {
		m_twin.setWinExtent(tsize);
	}

	setupWinAttrs();

	const auto parent = this->parent();
	const auto win = m_twin.winExtent();

	m_win_geometry.width = win.width;
	m_win_geometry.height = win.height;

	setupWindow(parent);
	createGraphicsContext(parent);
	resize(tsize);

	m_input.tryOpen();

	setupPointer();

	m_display.mapWindow(m_window);
	m_display.sync();

	m_selection.init();

	if (m_cmdline.useXSync()) {
		m_display.setSynchronized(true);
	}
}

void WindowSystem::applyConfig(TermSize &tsize) {
	m_font_manager.setFontSpec(m_cmdline.font.getValue());
	const auto config_file = m_nst.configFile();

	if (auto fontspec = config_file.asString("font"); fontspec && !m_cmdline.font.isSet()) {
		m_font_manager.setFontSpec(*fontspec);
	}

	if (!m_font_manager.loadFonts()) {
		cosmos_throw (cosmos::RuntimeError(cosmos::sprintf("Failed to open font %s", m_font_manager.fontSpec().c_str())));
	}

	if (auto pixels = config_file.asUnsigned("border_pixels"); pixels != std::nullopt) {
		auto num_pixels = std::min(*pixels, 100UL);
		m_border_pixels = num_pixels;
	}

	m_twin.setBorderPixels(m_border_pixels);
	m_twin.setCharSize(m_font_manager.normalFont());

	if (auto thickness = config_file.asUnsigned("cursor_thickness"); thickness != std::nullopt) {
		auto num_pixels = std::min(static_cast<int>(*thickness), m_twin.chrExtent().height / 2);
		m_cursor_thickness = num_pixels;
	}

	 auto toCursorStyle = [](std::string s) -> std::optional<CursorStyle>{
		cosmos::strip(s);
		s = cosmos::to_upper(s);
		if (s == "BLINKING_BLOCK")
			return CursorStyle::BLINKING_BLOCK;
		else if (s == "STEADY_BLOCK")
			return CursorStyle::STEADY_BLOCK;
		else if (s == "REVERSE_BLOCK")
			return CursorStyle::REVERSE_BLOCK;
		else if (s == "BLINKING_UNDERLINE")
			return CursorStyle::BLINKING_UNDERLINE;
		else if (s == "STEADY_UNDERLINE")
			return CursorStyle::STEADY_UNDERLINE;
		else if (s == "BLINKING_BAR")
			return CursorStyle::BLINKING_BAR;
		else if (s == "STEADY_BAR")
			return CursorStyle::STEADY_BAR;
		else if (s == "SNOWMAN")
			return CursorStyle::SNOWMAN;

		return std::nullopt;
	};

	auto &logger = m_nst.logger();

	if (auto shape_str = config_file.asString("cursor_shape"); shape_str != std::nullopt) {
		auto shape_opt = toCursorStyle(*shape_str);

		if (shape_opt) {
			setCursorStyle(*shape_opt);
		} else {
			logger.error() << "invalid cursor_shape setting '" << *shape_str << "'" << std::endl;
		}
	}

	if (auto rows = config_file.asUnsigned("rows"); rows != std::nullopt) {
		if (*rows < 1 || *rows > 1000) {
			logger.error() << "value for rows '" << *rows << "' is out of range" << std::endl;
		} else {
			tsize.rows = *rows;
		}
	}

	if (auto cols = config_file.asUnsigned("cols"); cols != std::nullopt) {
		if (*cols < 1 || *cols > 1000) {
			logger.error() << "value for cols '" << *cols << "' is out of range" << std::endl;
		} else {
			tsize.cols = *cols;
		}
	}
}

void WindowSystem::makeGlyphFontSpecs(const Glyph *glyphs, const size_t count, const CharPos char_pos) {
	const auto chr = m_twin.chrExtent();
	const auto start_pos = m_twin.toDrawPos(char_pos);
	Glyph::AttrBitMask prev_mode{Glyph::AttrBitMask::all};
	DrawPos cur_pos{start_pos};
	Font *font = nullptr;
	int runewidth = 0;
	GlyphFontSpec spec;

	m_font_specs.clear();

	for (size_t i = 0; i < count; i++) {
		const auto &glyph = glyphs[i];

		// Skip dummy wide-character spacing.
		if (glyph.isDummy())
			continue;

		// Determine font for glyph if different from previous glyph.
		if (const auto mode = glyph.mode; prev_mode != mode) {
			prev_mode = mode;
			font = m_font_manager.fontForMode(mode);
			runewidth = chr.width * glyph.width();
			cur_pos.y = start_pos.y + font->ascent();
		}

		m_font_manager.assignFont(glyph.rune, *font, spec);
		spec.setPos(cur_pos);

		m_font_specs.emplace_back(spec);
		cur_pos.moveRight(runewidth);
	}

	m_next_font_spec = m_font_specs.begin();
}

void WindowSystem::drawGlyphFontSpecs(Glyph base, const size_t count, const CharPos char_pos) {
	const auto pos = m_twin.toDrawPos(char_pos);
	const auto chr = m_twin.chrExtent();
	const int textwidth = count * base.width() * chr.width;

	cleanupWindowBorders(textwidth, char_pos, pos);

	m_font_manager.sanitize(base);
	m_color_manager.configureFor(base);

	// Clean up the region we want to draw to.
	m_font_draw_ctx.drawRect(m_color_manager.backColor(), pos, Extent{textwidth, chr.height});

	// Set the clip region because Xft is sometimes dirty.
	m_font_draw_ctx.setClipRectangle(pos, Extent{textwidth, chr.height});

	const auto &front_color = m_color_manager.frontColor();

	// Render the glyphs.
	m_font_draw_ctx.drawSpecs(front_color, m_next_font_spec, m_next_font_spec + count);

	// Render underline and strikethrough.
	if (base.isUnderlined()) {
		m_font_draw_ctx.drawRect(front_color, pos.atBelow(m_font_manager.ascent() * config::CH_SCALE + 1), Extent{textwidth, 1});
	}

	if (base.isStruck()) {
		m_font_draw_ctx.drawRect(front_color, pos.atBelow(2 * m_font_manager.ascent() * config::CH_SCALE / 3), Extent{textwidth, 1});
	}

	m_font_draw_ctx.resetClip();

	m_next_font_spec += count;
}

void WindowSystem::drawGlyph(const Glyph g, const CharPos pos) {
	makeGlyphFontSpecs(&g, 1, pos);
	drawGlyphFontSpecs(g, 1, pos);
}

void WindowSystem::drawGlyphs(Line::const_iterator it, const Line::const_iterator end, CharPos start_pos) {
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
			Glyph glyph;
			it < end && num_specs < specs_left;
			++it, cur_pos.moveRight()) {
		// we need to copy, because of the possible mode
		// flip below
		glyph = *it;

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
		}

		// for each new series make sure we have the proper reference
		if (num_specs == 0) {
			base = glyph;
		}

		num_specs++;
	}

	if (num_specs != 0) {
		drawGlyphFontSpecs(base, num_specs, start_pos);
	}
}

void WindowSystem::cleanupWindowBorders(const int textwidth, const CharPos char_pos, const DrawPos draw_pos) {
	const auto chr = m_twin.chrExtent();
	const auto tty = m_twin.TTYExtent();
	const auto win = m_twin.winExtent();
	const bool reaches_bottom_border = draw_pos.y + chr.height >= m_border_pixels + tty.height;

	// NOTE: it is not fully clear why the window borders should get dirty
	// in the first place.

	// left border
	if (char_pos.x == 0) {
		const auto pos1 = DrawPos{0, char_pos.y ? draw_pos.y : 0};
		const auto pos2 = DrawPos{
			m_border_pixels,
			char_pos.y + chr.height + (reaches_bottom_border ? win.height : 0)
		};
		clearRect(pos1, pos2);
	}

	// right border
	if (draw_pos.x + textwidth >= m_border_pixels + tty.width) {
		const auto pos1 = DrawPos{
			draw_pos.x + textwidth,
			char_pos.y ? draw_pos.y : 0};
		const auto pos2 = DrawPos{
			win.width,
			reaches_bottom_border ? win.height : draw_pos.y + chr.height
		};
		clearRect(pos1, pos2);
	}

	// top border
	if (char_pos.y == 0) {
		clearRect(
				DrawPos{draw_pos.x, 0},
				DrawPos{draw_pos.x + textwidth, m_border_pixels});
	}

	// bottom border
	if (draw_pos.y + chr.height >= m_border_pixels + tty.height) {
		clearRect(
				DrawPos{draw_pos.x, draw_pos.y + chr.height},
				DrawPos{draw_pos.x + textwidth, win.height});
	}
}

void WindowSystem::setupPointer() {

	auto parseColor = [this](const ColorIndex idx, const unsigned short fallback) {
		xpp::XColor ret;
		auto name = get_color_name(idx);
		try {
			m_display.parseColor(ret, name);
		} catch (const cosmos::CosmosError &) {
			ret.setAll(fallback);
		}

		return ret;
	};

	// white cursor, black outline
	const auto fg = parseColor(config::MOUSE_FG, 0xFFFF);
	const auto bg = parseColor(config::MOUSE_BG, 0x0000);

	m_font_pointer = xpp::XCursor{config::MOUSE_SHAPE};
	m_font_pointer.recolorCursor(fg, bg);
	m_window.defineCursor(m_font_pointer);

	xpp::Pixmap blank_pm{xpp::to_drawable(m_window.id()), "", {1, 1}};
	m_blank_pointer = xpp::XCursor{blank_pm, &blank_pm, fg, bg, xpp::Coord{0, 0}};
}

void WindowSystem::hidePointer() {
	if (config::HIDE_MOUSE_CURSOR && m_is_pointer_visible) {
		m_is_pointer_visible = false;
		m_window.defineCursor(m_blank_pointer);
		setPointerMotion(true);
	}
}

void WindowSystem::showPointer() {
	if (!m_is_pointer_visible) {
		m_is_pointer_visible = true;
		m_window.defineCursor(m_font_pointer);
		if (!m_twin.reportMouseMany()) {
			setPointerMotion(false);
		}
	}
}

void WindowSystem::invertColors() {
	m_twin.flipFlag(WinMode::REVERSE);
	m_nst.term().redraw();
}

void WindowSystem::clearCursor(const CharPos pos, Glyph glyph) {
	if (m_nst.selection().isSelected(pos))
		glyph.mode.flip(Attr::REVERSE);
	drawGlyph(glyph, pos);
}

void WindowSystem::drawCursor(const CharPos pos, Glyph glyph) {

	auto &color = m_color_manager.applyCursorColor(m_nst.selection().isSelected(pos), glyph);
	const auto chr = m_twin.chrExtent();

	if (m_twin.hideCursor()) {
		return;
	} else if (m_twin.isFocused()) {
		if (m_blinking_cursor_style && m_twin.inBlinkMode()) {
			return;
		}

		switch (m_twin.getCursorStyle()) {
			case CursorStyle::SNOWMAN: // st extension
				// NOTE: this means when moving the cursor
				// over existing text, that the text will no
				// longer be visible.
				glyph.rune = 0x2603; // snowman (U+2603)
			/* FALLTHROUGH */
			case CursorStyle::BLINKING_BLOCK:
			case CursorStyle::BLINKING_BLOCK_DEFAULT:
			case CursorStyle::STEADY_BLOCK:
			case CursorStyle::REVERSE_BLOCK:
				drawGlyph(glyph, pos);
				break;
			case CursorStyle::BLINKING_UNDERLINE:
			case CursorStyle::STEADY_UNDERLINE: {
				auto dpos = m_twin.toDrawPos(pos.nextLine());
				dpos.moveUp(m_cursor_thickness);
				m_font_draw_ctx.drawRect(color, dpos, Extent{chr.width, m_cursor_thickness});
				break;
			}
			case CursorStyle::BLINKING_BAR:
			case CursorStyle::STEADY_BAR: {
				auto dpos = m_twin.toDrawPos(pos);
				m_font_draw_ctx.drawRect(color, dpos, Extent{m_cursor_thickness, chr.height});
				break;
			}
			default: // unknown cursor style
				break;
		}
	} else {
		// only draw a non-solid rectangle outline of the cursor, if there's no focus
		auto dpos = m_twin.toDrawPos(pos);
		// upper part
		m_font_draw_ctx.drawRect(color, dpos, Extent{chr.width - 1, 1});
		// left part
		m_font_draw_ctx.drawRect(color, dpos, Extent{1, chr.height - 1});

		// right part
		auto nextcol = m_twin.nextCol(dpos).atLeft(1);
		m_font_draw_ctx.drawRect(color, nextcol, Extent{1, chr.height - 1});

		// lower part
		auto nextline = m_twin.nextLine(dpos).atAbove(1);
		m_font_draw_ctx.drawRect(color, nextline, Extent{chr.width, 1});
	}
}

void WindowSystem::setDefaultIconTitle() {
	setIconTitle(m_cmdline.title());
}

void WindowSystem::setIconTitle(const std::string_view title) {
	xpp::Property<xpp::utf8_string> data{xpp::utf8_string(title)};
	m_window.setProperty(xpp::atoms::wm_icon_name, data);
	m_window.setProperty(xpp::atoms::ewmh_icon_name, data);
}

void WindowSystem::setDefaultTitle() {
	setTitle(m_cmdline.title());
}

void WindowSystem::setTitle(const std::string_view title) {
	xpp::Property<xpp::utf8_string> data{xpp::utf8_string(title)};
	m_window.setProperty(xpp::atoms::icccm_window_name, data);
	m_window.setProperty(xpp::atoms::ewmh_window_name, data);
}

void WindowSystem::toggleFullScreen() {
	xpp::Event raw_ev{xpp::EventType::CLIENT_MESSAGE};
	xpp::NetWmStateEvent event{raw_ev};

	event.setDisplay(xpp::display);
	event.setWinID(m_window.id());
	event.setOperation(xpp::NetWmStateAction::TOGGLE);
	event.setProperties(xpp::atoms::ewmh_wm_state_fullscreen);

	auto root = xpp::RootWin{m_display, xpp::screen};
	root.sendEvent(raw_ev);
}

void WindowSystem::pushTitle() {
	if (MAX_TITLE_STACK_SIZE == 0)
		return;

	while (m_title_stack.size() >= MAX_TITLE_STACK_SIZE) {
		m_title_stack.pop_back();
	}

	// retrieve the current window title from X11, since an external
	// program can change the title outside of the terminal context.
	xpp::Property<xpp::utf8_string> data;
	m_window.getProperty(xpp::atoms::ewmh_window_name, data);
	m_title_stack.push_front(data.raw());
}

void WindowSystem::popTitle() {
	if (m_title_stack.empty())
		return;

	setTitle(m_title_stack.front());
	m_title_stack.pop_front();
}

void WindowSystem::finishDraw() {
	const auto extent = m_twin.winExtent();
	const auto &color = m_color_manager.fontColor(m_twin.activeForegroundColor());

	m_window.copyArea(m_graphics_context, m_pixmap, extent);
	m_graphics_context.setForeground(color.index());
}

void WindowSystem::changeEventMask(const xpp::EventMask event, bool on_off) {
	m_win_attrs.changeEventMask(event, on_off);
	m_window.setWindowAttrs(m_win_attrs, xpp::WindowAttrMask{xpp::WindowAttr::EVENT_MASK});
}

void WindowSystem::setMode(const WinMode flag, const bool set) {
	const auto prevmode = m_twin.mode();
	m_twin.setFlag(flag, set);
	if (m_twin.mode()[WinMode::REVERSE] != prevmode[WinMode::REVERSE])
		m_nst.term().redraw();
}

void WindowSystem::setCursorStyle(const CursorStyle cursor) {
	m_twin.setCursorStyle(cursor);
	m_blinking_cursor_style = is_blinking_cursor(cursor);
}

void WindowSystem::setUrgency(const bool have_urgency) {
	auto hints = m_window.getWMHints();

	// should never be nullptr, since we've set hints initially
	if (!hints)
		return;

	hints->changeFlag(xpp::WindowManagerHints::Flags::URGENCY, have_urgency);

	m_window.setWMHints(*hints);
}

void WindowSystem::ringBell() {
	if (!(m_twin.checkFlag(WinMode::FOCUSED)))
		setUrgency(true);
	if (config::BELL_VOLUME != xpp::BellVolume::NONE) {
		xpp::ring_bell(m_window, config::BELL_VOLUME);
	}
}

void WindowSystem::embeddedFocusChange(const bool in_focus) {
	// called when we run embedded in another window and the focus changes
	if (in_focus) {
		m_twin.setFlag(WinMode::FOCUSED);
		setUrgency(false);
	} else {
		m_twin.resetFlag(WinMode::FOCUSED);
	}
}

void WindowSystem::focusChange(const bool in_focus) {
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
