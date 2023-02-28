// C++
#include <functional>

// nst
#include "nst.hxx"
#include "Selection.hxx"
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

	return {
			    /* mask                  button   function                              release */
		MouseShortcut{ XK_ANY_MOD,           Button2, std::bind(&X11::pasteSelection, &x11), true },
		MouseShortcut{ ShiftMask,            Button4, std::bind(ttysend, "\033[5;2~"),      false },
		MouseShortcut{ XK_ANY_MOD,           Button4, std::bind(ttysend, "\031"),           false },
		MouseShortcut{ ShiftMask,            Button5, std::bind(ttysend, "\033[6;2~"),      false },
		MouseShortcut{ XK_ANY_MOD,           Button5, std::bind(ttysend, "\005"),           false },
	};
}

std::vector<KbdShortcut> get_kbd_shortcuts(Nst &nst) {
	/* Internal keyboard shortcuts. */
	//constexpr auto MODKEY = Mod1Mask;
	constexpr auto TERMMOD = ControlMask|ShiftMask;

	auto &tty = nst.tty();
	auto &x11 = nst.x11();
	auto &term = nst.term();
	auto selPaste = std::bind(&X11::pasteSelection, &x11);

	auto togglePrinter = [&]() { term.setPrintMode(!term.isPrintMode()); };
	auto printScreen = [&]() { term.dump(); };
	auto printSel = [&]() { nst.selection().dump(); };

	return {
		/* mask                 keysym          function */
		{ XK_ANY_MOD,           XK_Break,       std::bind(&TTY::sendBreak, &tty) },
		{ ControlMask,          XK_Print,       togglePrinter       },
		{ ShiftMask,            XK_Print,       printScreen         },
		{ XK_ANY_MOD,           XK_Print,       printSel            },
		{ TERMMOD,              XK_Prior,       std::bind(&X11::zoomFont, &x11, +1) },
		{ TERMMOD,              XK_Next,        std::bind(&X11::zoomFont, &x11, -1) },
		{ TERMMOD,              XK_Home,        std::bind(&X11::resetFont, &x11) },
		{ TERMMOD,              XK_C,           std::bind(&X11::copyToClipboard, &x11) },
		{ TERMMOD,              XK_V,           std::bind(&X11::pasteClipboard, &x11) },
		{ TERMMOD,              XK_Y,           selPaste            },
		{ ShiftMask,            XK_Insert,      selPaste            },
		{ TERMMOD,              XK_Num_Lock,    std::bind(&X11::toggleNumlock, &x11) },
	};
}

const std::string_view get_color_name(size_t nr) {
	if (nr < COLORNAMES.size())
		return COLORNAMES[nr];
	else if (nr >= 256) {
		// check for extended colors
		nr -= 256;
		if (nr < EXTENDED_COLORS.size())
			return EXTENDED_COLORS[nr];
	}

	// unassigned
	// NOTE: the libX functions that consume this are tolerant against
	// null pointers
	return {};
}

} // ns nst::config
