// C++
#include <functional>

// nst
#include "nst.hxx"
#include "x11.hxx"

/*
 * the implementation of these are placed in this separate file since they
 * need data structures that would cause circular dependencies when included
 * in the config header
 */

namespace nst::config {

/*
 * Internal mouse shortcuts.
 * Beware that overloading Button1 will disable the selection.
 */
std::vector<MouseShortcut> get_mouse_shortcuts(Nst &nst) {

	auto ttysend = [&](const char *s) {
		auto &tty = nst.tty();
		tty.write(s, TTY::MayEcho{true});
	};

	auto &x11 = nst.x11();

	using xpp::Button;

	return {
		//             mask                  button           function                              release
		MouseShortcut{ XK_ANY_MOD,           Button::BUTTON2, std::bind(&X11::pasteSelection, &x11), true },
		MouseShortcut{ ShiftMask,            Button::BUTTON4, std::bind(ttysend, "\033[5;2~"),      false },
		MouseShortcut{ XK_ANY_MOD,           Button::BUTTON4, std::bind(ttysend, "\031"),           false },
		MouseShortcut{ ShiftMask,            Button::BUTTON5, std::bind(ttysend, "\033[6;2~"),      false },
		MouseShortcut{ XK_ANY_MOD,           Button::BUTTON5, std::bind(ttysend, "\005"),           false },
	};
}

std::vector<KbdShortcut> get_kbd_shortcuts(Nst &nst) {
	// Internal keyboard shortcuts.
	//constexpr auto MODKEY = Mod1Mask;
	constexpr unsigned int TERMMOD = ControlMask|ShiftMask;

	auto &tty = nst.tty();
	auto &x11 = nst.x11();
	auto &term = nst.term();
	auto selPaste = std::bind(&X11::pasteSelection, &x11);

	auto togglePrinter = [&]() { term.setPrintMode(!term.isPrintMode()); };
	auto printScreen = [&]() { term.dump(); };
	auto printSel = [&]() { nst.selection().dump(); };

	return {
		// mask                 keysym              function
		{ XK_ANY_MOD,           KeyID::BREAK,       std::bind(&TTY::sendBreak, &tty) },
		{ ControlMask,          KeyID::PRINT,       togglePrinter       },
		{ ShiftMask,            KeyID::PRINT,       printScreen         },
		{ XK_ANY_MOD,           KeyID::PRINT,       printSel            },
		{ TERMMOD,              KeyID::PRIOR,       std::bind(&X11::zoomFont, &x11, +1) },
		{ TERMMOD,              KeyID::NEXT,        std::bind(&X11::zoomFont, &x11, -1) },
		{ TERMMOD,              KeyID::HOME,        std::bind(&X11::resetFont, &x11) },
		{ TERMMOD,              KeyID::C,           std::bind(&X11::copyToClipboard, &x11) },
		{ TERMMOD,              KeyID::V,           std::bind(&X11::pasteClipboard, &x11) },
		{ TERMMOD,              KeyID::Y,           selPaste            },
		{ ShiftMask,            KeyID::INSERT,      selPaste            },
		{ TERMMOD,              KeyID::NUM_LOCK,    std::bind(&X11::toggleNumlock, &x11) },
	};
}

const std::string_view get_color_name(const ColorIndex idx) {
	if (auto raw = cosmos::to_integral(idx); raw < COLORNAMES.size()) {
		return COLORNAMES[raw];
	} else if (idx >= ColorIndex::START_EXTENDED) {
		const auto ext = cosmos::to_integral(idx - ColorIndex::START_EXTENDED);
		// check for extended colors
		if (ext < EXTENDED_COLORS.size())
			return EXTENDED_COLORS[ext];
	}

	// unassigned
	// NOTE: the libX functions that consume this are tolerant against null pointers
	return {};
}

} // ns nst::config
