// C++
#include <functional>

// nst
#include "nst.hxx"
#include "WindowSystem.hxx"

/*
 * the implementation of these are placed in this separate file since they
 * need data structures that would cause circular dependencies when included
 * in the config header
 */

namespace nst::config {

static_assert(COLS > 1);
static_assert(ROWS > 1);

/// Internal mouse shortcuts.
/**
 * Beware that overloading Button1 will disable the selection behaviour.
 **/
std::vector<MouseShortcut> get_mouse_shortcuts(Nst &nst) {

	auto &tty = nst.tty();
	auto &wsys = nst.wsys();

	auto ttysend = [&](const std::string_view s) {
		tty.write(s, TTY::MayEcho{true});
	};

	using xpp::Button;

	return {
		//             mask              button           function                              release
		MouseShortcut{ Mask{Mod::ANY},   Button::BUTTON2, std::bind(&WindowSystem::pasteSelection, &wsys), true },
		MouseShortcut{ Mask{Mod::SHIFT}, Button::BUTTON4, std::bind(ttysend, "\033[5;2~"),      false },
		MouseShortcut{ Mask{Mod::ANY},   Button::BUTTON4, std::bind(ttysend, "\031"),           false },
		MouseShortcut{ Mask{Mod::SHIFT}, Button::BUTTON5, std::bind(ttysend, "\033[6;2~"),      false },
		MouseShortcut{ Mask{Mod::ANY},   Button::BUTTON5, std::bind(ttysend, "\005"),           false },
	};
}

/// Internal keyboard shortcuts.
std::vector<KbdShortcut> get_kbd_shortcuts(Nst &nst) {
	//constexpr auto MODKEY = Mod1Mask;
	constexpr xpp::InputMask TERMMOD{Mod::CONTROL, Mod::SHIFT};

	auto &tty = nst.tty();
	auto &wsys = nst.wsys();
	auto &term = nst.term();
	auto selPaste = std::bind(&WindowSystem::pasteSelection, &wsys);

	auto togglePrinter = [&]() { term.setPrintMode(!term.isPrintMode()); };
	auto printScreen   = [&]() { term.dump(); };
	auto printSel      = [&]() { nst.selection().dump(); };

	return {
		// mask                 keysym              function
		{ Mask{Mod::ANY},       KeyID::BREAK,       std::bind(&TTY::sendBreak, &tty) },
		{ Mask{Mod::CONTROL},   KeyID::PRINT,       togglePrinter       },
		{ Mask{Mod::SHIFT},     KeyID::PRINT,       printScreen         },
		{ Mask{Mod::ANY},       KeyID::PRINT,       printSel            },
		{ TERMMOD,              KeyID::PRIOR,       std::bind(&WindowSystem::zoomFont, &wsys, +1) },
		{ TERMMOD,              KeyID::NEXT,        std::bind(&WindowSystem::zoomFont, &wsys, -1) },
		{ TERMMOD,              KeyID::HOME,        std::bind(&WindowSystem::resetFont, &wsys) },
		{ TERMMOD,              KeyID::C,           std::bind(&WindowSystem::copyToClipboard, &wsys) },
		{ TERMMOD,              KeyID::V,           std::bind(&WindowSystem::pasteClipboard, &wsys) },
		{ TERMMOD,              KeyID::Y,           selPaste            },
		{ Mask{Mod::SHIFT},     KeyID::INSERT,      selPaste            },
		{ TERMMOD,              KeyID::NUM_LOCK,    std::bind(&WindowSystem::toggleNumlock, &wsys) },
		{ Mask{Mod::SHIFT},     KeyID::PRIOR,       std::bind(&nst::Term::scrollHistoryUp, &term, +5) },
		{ Mask{Mod::SHIFT},     KeyID::NEXT,        std::bind(&nst::Term::scrollHistoryDown, &term, +5) },
	};
}

} // ns nst::config
