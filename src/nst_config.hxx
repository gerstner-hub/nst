#ifndef NST_CONFIG_H
#define NST_CONFIG_H

// libc
#include <limits.h>

// C++
#include <array>
#include <chrono>
#include <set>
#include <string_view>

// X++
#include "X++/XCursor.hxx"
#include "X++/keyboard.hxx"
#include "X++/types.hxx"

// nst
#include "Selection.hxx"
#include "types.hxx"

namespace nst {

class Nst; // fwd. decl

namespace config {

// X modifiers
inline constexpr unsigned int XK_ANY_MOD = UINT_MAX;
inline constexpr unsigned int XK_NO_MOD = 0;
inline constexpr unsigned int XK_SWITCH_MOD = (1<<13|1<<14);

/*
 * appearance
 *
 * font: see http://freedesktop.org/software/fontconfig/fontconfig-user.html
 */
constexpr std::string_view FONT{"Liberation Mono:pixelsize=12:antialias=true:autohint=true"};
constexpr double FONT_DEFAULT_SIZE_PX = 12;

/*
 * word delimiter string
 *
 * More advanced example: L" `'\"()[]{}"
 */
constexpr const std::wstring_view WORD_DELIMITERS{L" "};

constexpr std::array<std::string_view, 8> STTY_ARGS{{"stty", "raw", "pass8", "nl", "-echo", "-iexten", "-cstopb", "38400"}};

/*
 * What program is execed by st depends on these precedence rules:
 * 1: program passed with -e
 * 2: scroll and/or utmp
 * 3: SHELL environment variable
 * 4: value of shell in /etc/passwd
 * 5: value of shell in config.h
 */
constexpr std::string_view SHELL{"/bin/sh"};
constexpr std::string_view UTMP{};
/* scroll program: to enable use a string like "scroll" */
constexpr std::string_view SCROLL{};
/* default TERM value */
constexpr std::string_view TERM_NAME{"st-256color"};

/* identification sequence returned in DA and DECID */
constexpr std::string_view VT_IDENT{"\033[?6c"};

/* allow certain non-interactive (insecure) window operations such as:
   setting the clipboard text */
constexpr bool ALLOW_WINDOW_OPS = false;

/*
 * spaces per tab
 *
 * When you are changing this value, don't forget to adapt the »it« value in
 * the st.info and appropriately install the st.info in the environment where
 * you use this st version.
 *
 *	it#$TABSPACES,
 *
 * Secondly make sure your kernel is not expanding tabs. When running `stty
 * -a` »tab0« should appear. You can tell the terminal to not expand tabs by
 *  running following command:
 *
 *	stty tabs
 */
constexpr int TABSPACES = 8;

/*
 * Default colors (colorname index)
 * foreground, background, cursor, reverse cursor
 */
constexpr ColorIndex DEFAULT_FG{258};
constexpr ColorIndex DEFAULT_BG{259};
constexpr ColorIndex DEFAULT_CS{256};
constexpr ColorIndex DEFAULT_RCS{257};

/* alt screens */
constexpr bool ALLOW_ALTSCREEN = true;

constexpr int BORDERPX = 2;

/* Kerning / character bounding-box multipliers */
constexpr float CW_SCALE = 1.0;
constexpr float CH_SCALE = 1.0;

/* selection timeouts (in milliseconds) */
constexpr std::chrono::milliseconds DOUBLE_CLICK_TIMEOUT{300};
constexpr std::chrono::milliseconds TRIPLE_CLICK_TIMEOUT{600};

/*
 * Set this to true if you want the selection to disappear when you select
 * something different in another window.
 */
constexpr bool SEL_CLEAR = false;

/*
 * draw latency range - from new content/keypress/etc until drawing.
 * within this range, nst draws when content stops arriving (idle). mostly it's
 * near MINLATENCY, but it waits longer for slow updates to avoid partial draw.
 * low MINLATENCY Will tear/flicker more, as it can "detect" idle too early.
 */
constexpr std::chrono::milliseconds MIN_LATENCY{8};
constexpr std::chrono::milliseconds MAX_LATENCY{33};

/*
 * blinking timeout (set to 0 to disable blinking) for the terminal blinking
 * attribute.
 */
constexpr std::chrono::milliseconds BLINK_TIMEOUT{800};

/*
 * thickness of underline and bar cursors
 */
constexpr unsigned int CURSOR_THICKNESS = 2;

/*
 * bell volume. It must be a value between -100 and 100. Use 0 for disabling
 * it
 */
constexpr xpp::BellVolume BELL_VOLUME{0};

constexpr std::array<std::string_view, 4> EXTENDED_COLORS{{
	/* more colors can be added after 255 to use with DefaultXX */
	"#cccccc",
	"#555555",
	"gray90", /* default foreground colour */
	"black"   /* default background colour */
}};

/* Terminal colors (16 first used in escape sequence) */
constexpr std::array<std::string_view, 16> COLORNAMES{{
	/* 8 normal colors */
	"black",
	"red3",
	"green3",
	"yellow3",
	"blue2",
	"magenta3",
	"cyan3",
	"gray90",

	/* 8 bright colors */
	"gray50",
	"red",
	"green",
	"yellow",
	"#5c5cff",
	"magenta",
	"cyan",
	"white",
}};

/// returns the color name for a color number taking into account extended color configuration
/**
 * \return The according color name or nullptr if none is configured for the number
 **/
const std::string_view get_color_name(ColorIndex idx);

/// Default shape of cursor
static constexpr CursorStyle CURSORSHAPE = CursorStyle::STEADY_BLOCK;

/*
 * Default columns and rows numbers
 */

static constexpr unsigned int COLS = 80;
static constexpr unsigned int ROWS = 24;

static_assert(COLS > 1);
static_assert(ROWS > 1);

/*
 * Default colour and shape of the mouse cursor
 */
constexpr xpp::CursorFont MOUSE_SHAPE{xpp::CursorFont::XTERM};
constexpr ColorIndex MOUSE_FG{7};
constexpr ColorIndex MOUSE_BG{0};

/*
 * Color used to display font attributes when fontconfig selected a font which
 * doesn't match the ones requested.
 */
constexpr ColorIndex DEFAULT_ATTR{11};

/*
 * Force mouse select/shortcuts while mask is active (when MODE_MOUSE is set).
 * Note that if you want to use ShiftMask with selmasks, set this to an other
 * modifier, set to 0 to not use it.
 */
constexpr xpp::InputMask FORCE_MOUSE_MOD{xpp::InputModifier::SHIFT};

/*
 * Special keys (change & recompile st.info accordingly)
 *
 * Mask value:
 * * Use XK_ANY_MOD to match the key no matter modifiers state
 * * Use XK_NO_MOD to match the key alone (no modifiers)
 * appkey value:
 * * 0: no value
 * * > 0: keypad application mode enabled
 * *   = 2: term.numlock = 1
 * * < 0: keypad application mode disabled
 * appcursor value:
 * * 0: no value
 * * > 0: cursor application mode enabled
 * * < 0: cursor application mode disabled
 *
 * Be careful with the order of the definitions because st searches in
 * this table sequentially, so any XK_ANY_MOD must be in the last
 * position for a key.
 */

/*
 * If you want keys other than the X11 function keys (0xFD00 - 0xFFFF)
 * to be mapped below, add them to this array.
 */
const std::set<xpp::KeySymID> MAPPED_KEYS{
};

/*
 * State bits to ignore when matching key or button events.  By default,
 * numlock (Mod2Mask) and keyboard layout (XK_SWITCH_MOD) are ignored.
 */
constexpr unsigned int IGNOREMOD = Mod2Mask|XK_SWITCH_MOD;

// we use a multiset for the key definitions below
// the keysym is the comparison key, so we don't have to iterate over the
// complete list of keys linearly, but only over a small list of key
// combinations that share the same keysym.
struct KeyCmp {
	bool operator()(const Key &a, const Key &b) const {
		return a.k < b.k;
	}
};

using KeyID = xpp::KeySymID;

/*
 * This is the huge list of keys which defines all compatibility to the Linux
 * world. Please decide about changes wisely.
 */
const std::multiset<Key, KeyCmp> KEYS{{
	// keysym               mask            string           appkey appcursor
	{ KeyID::KP_HOME,       ShiftMask,      "\033[2J",       0,   -1},
	{ KeyID::KP_HOME,       ShiftMask,      "\033[1;2H",     0,   +1},
	{ KeyID::KP_HOME,       XK_ANY_MOD,     "\033[H",        0,   -1},
	{ KeyID::KP_HOME,       XK_ANY_MOD,     "\033[1~",       0,   +1},
	{ KeyID::KP_UP,         XK_ANY_MOD,     "\033Ox",       +1,    0},
	{ KeyID::KP_UP,         XK_ANY_MOD,     "\033[A",        0,   -1},
	{ KeyID::KP_UP,         XK_ANY_MOD,     "\033OA",        0,   +1},
	{ KeyID::KP_DOWN,       XK_ANY_MOD,     "\033Or",       +1,    0},
	{ KeyID::KP_DOWN,       XK_ANY_MOD,     "\033[B",        0,   -1},
	{ KeyID::KP_DOWN,       XK_ANY_MOD,     "\033OB",        0,   +1},
	{ KeyID::KP_LEFT,       XK_ANY_MOD,     "\033Ot",       +1,    0},
	{ KeyID::KP_LEFT,       XK_ANY_MOD,     "\033[D",        0,   -1},
	{ KeyID::KP_LEFT,       XK_ANY_MOD,     "\033OD",        0,   +1},
	{ KeyID::KP_RIGHT,      XK_ANY_MOD,     "\033Ov",       +1,    0},
	{ KeyID::KP_RIGHT,      XK_ANY_MOD,     "\033[C",        0,   -1},
	{ KeyID::KP_RIGHT,      XK_ANY_MOD,     "\033OC",        0,   +1},
	{ KeyID::KP_PRIOR,      ShiftMask,      "\033[5;2~",     0,    0},
	{ KeyID::KP_PRIOR,      XK_ANY_MOD,     "\033[5~",       0,    0},
	{ KeyID::KP_BEGIN,      XK_ANY_MOD,     "\033[E",        0,    0},
	{ KeyID::KP_END,        ControlMask,    "\033[J",       -1,    0},
	{ KeyID::KP_END,        ControlMask,    "\033[1;5F",    +1,    0},
	{ KeyID::KP_END,        ShiftMask,      "\033[K",       -1,    0},
	{ KeyID::KP_END,        ShiftMask,      "\033[1;2F",    +1,    0},
	{ KeyID::KP_END,        XK_ANY_MOD,     "\033[4~",       0,    0},
	{ KeyID::KP_NEXT,       ShiftMask,      "\033[6;2~",     0,    0},
	{ KeyID::KP_NEXT,       XK_ANY_MOD,     "\033[6~",       0,    0},
	{ KeyID::KP_INSERT,     ShiftMask,      "\033[2;2~",    +1,    0},
	{ KeyID::KP_INSERT,     ShiftMask,      "\033[4l",      -1,    0},
	{ KeyID::KP_INSERT,     ControlMask,    "\033[L",       -1,    0},
	{ KeyID::KP_INSERT,     ControlMask,    "\033[2;5~",    +1,    0},
	{ KeyID::KP_INSERT,     XK_ANY_MOD,     "\033[4h",      -1,    0},
	{ KeyID::KP_INSERT,     XK_ANY_MOD,     "\033[2~",      +1,    0},
	{ KeyID::KP_DELETE,     ControlMask,    "\033[M",       -1,    0},
	{ KeyID::KP_DELETE,     ControlMask,    "\033[3;5~",    +1,    0},
	{ KeyID::KP_DELETE,     ShiftMask,      "\033[2K",      -1,    0},
	{ KeyID::KP_DELETE,     ShiftMask,      "\033[3;2~",    +1,    0},
	{ KeyID::KP_DELETE,     XK_ANY_MOD,     "\033[P",       -1,    0},
	{ KeyID::KP_DELETE,     XK_ANY_MOD,     "\033[3~",      +1,    0},
	{ KeyID::KP_MULTIPLY,   XK_ANY_MOD,     "\033Oj",       +2,    0},
	{ KeyID::KP_ADD,        XK_ANY_MOD,     "\033Ok",       +2,    0},
	{ KeyID::KP_ENTER,      XK_ANY_MOD,     "\033OM",       +2,    0},
	{ KeyID::KP_ENTER,      XK_ANY_MOD,     "\r",           -1,    0},
	{ KeyID::KP_SUBTRACT,   XK_ANY_MOD,     "\033Om",       +2,    0},
	{ KeyID::KP_DECIMAL,    XK_ANY_MOD,     "\033On",       +2,    0},
	{ KeyID::KP_DIVIDE,     XK_ANY_MOD,     "\033Oo",       +2,    0},
	{ KeyID::KP_0,          XK_ANY_MOD,     "\033Op",       +2,    0},
	{ KeyID::KP_1,          XK_ANY_MOD,     "\033Oq",       +2,    0},
	{ KeyID::KP_2,          XK_ANY_MOD,     "\033Or",       +2,    0},
	{ KeyID::KP_3,          XK_ANY_MOD,     "\033Os",       +2,    0},
	{ KeyID::KP_4,          XK_ANY_MOD,     "\033Ot",       +2,    0},
	{ KeyID::KP_5,          XK_ANY_MOD,     "\033Ou",       +2,    0},
	{ KeyID::KP_6,          XK_ANY_MOD,     "\033Ov",       +2,    0},
	{ KeyID::KP_7,          XK_ANY_MOD,     "\033Ow",       +2,    0},
	{ KeyID::KP_8,          XK_ANY_MOD,     "\033Ox",       +2,    0},
	{ KeyID::KP_9,          XK_ANY_MOD,     "\033Oy",       +2,    0},
	{ KeyID::UP,            ShiftMask,      "\033[1;2A",     0,    0},
	{ KeyID::UP,            Mod1Mask,       "\033[1;3A",     0,    0},
	{ KeyID::UP,         ShiftMask|Mod1Mask,"\033[1;4A",     0,    0},
	{ KeyID::UP,            ControlMask,    "\033[1;5A",     0,    0},
	{ KeyID::UP,      ShiftMask|ControlMask,"\033[1;6A",     0,    0},
	{ KeyID::UP,       ControlMask|Mod1Mask,"\033[1;7A",     0,    0},
	{ KeyID::UP,ShiftMask|ControlMask|Mod1Mask,"\033[1;8A",  0,    0},
	{ KeyID::UP,            XK_ANY_MOD,     "\033[A",        0,   -1},
	{ KeyID::UP,            XK_ANY_MOD,     "\033OA",        0,   +1},
	{ KeyID::DOWN,          ShiftMask,      "\033[1;2B",     0,    0},
	{ KeyID::DOWN,          Mod1Mask,       "\033[1;3B",     0,    0},
	{ KeyID::DOWN,       ShiftMask|Mod1Mask,"\033[1;4B",     0,    0},
	{ KeyID::DOWN,          ControlMask,    "\033[1;5B",     0,    0},
	{ KeyID::DOWN,    ShiftMask|ControlMask,"\033[1;6B",     0,    0},
	{ KeyID::DOWN,     ControlMask|Mod1Mask,"\033[1;7B",     0,    0},
	{ KeyID::DOWN,ShiftMask|ControlMask|Mod1Mask,"\033[1;8B",0,    0},
	{ KeyID::DOWN,          XK_ANY_MOD,     "\033[B",        0,   -1},
	{ KeyID::DOWN,          XK_ANY_MOD,     "\033OB",        0,   +1},
	{ KeyID::LEFT,          ShiftMask,      "\033[1;2D",     0,    0},
	{ KeyID::LEFT,          Mod1Mask,       "\033[1;3D",     0,    0},
	{ KeyID::LEFT,       ShiftMask|Mod1Mask,"\033[1;4D",     0,    0},
	{ KeyID::LEFT,          ControlMask,    "\033[1;5D",     0,    0},
	{ KeyID::LEFT,    ShiftMask|ControlMask,"\033[1;6D",     0,    0},
	{ KeyID::LEFT,     ControlMask|Mod1Mask,"\033[1;7D",     0,    0},
	{ KeyID::LEFT,ShiftMask|ControlMask|Mod1Mask,"\033[1;8D",0,    0},
	{ KeyID::LEFT,          XK_ANY_MOD,     "\033[D",        0,   -1},
	{ KeyID::LEFT,          XK_ANY_MOD,     "\033OD",        0,   +1},
	{ KeyID::RIGHT,         ShiftMask,      "\033[1;2C",     0,    0},
	{ KeyID::RIGHT,         Mod1Mask,       "\033[1;3C",     0,    0},
	{ KeyID::RIGHT,      ShiftMask|Mod1Mask,"\033[1;4C",     0,    0},
	{ KeyID::RIGHT,         ControlMask,    "\033[1;5C",     0,    0},
	{ KeyID::RIGHT,   ShiftMask|ControlMask,"\033[1;6C",     0,    0},
	{ KeyID::RIGHT,    ControlMask|Mod1Mask,"\033[1;7C",     0,    0},
	{ KeyID::RIGHT,ShiftMask|ControlMask|Mod1Mask,"\033[1;8C",0,   0},
	{ KeyID::RIGHT,         XK_ANY_MOD,     "\033[C",        0,   -1},
	{ KeyID::RIGHT,         XK_ANY_MOD,     "\033OC",        0,   +1},
	{ KeyID::ISO_LEFT_TAB,  ShiftMask,      "\033[Z",        0,    0},
	{ KeyID::RETURN,        Mod1Mask,       "\033\r",        0,    0},
	{ KeyID::RETURN,        XK_ANY_MOD,     "\r",            0,    0},
	{ KeyID::INSERT,        ShiftMask,      "\033[4l",      -1,    0},
	{ KeyID::INSERT,        ShiftMask,      "\033[2;2~",    +1,    0},
	{ KeyID::INSERT,        ControlMask,    "\033[L",       -1,    0},
	{ KeyID::INSERT,        ControlMask,    "\033[2;5~",    +1,    0},
	{ KeyID::INSERT,        XK_ANY_MOD,     "\033[4h",      -1,    0},
	{ KeyID::INSERT,        XK_ANY_MOD,     "\033[2~",      +1,    0},
	{ KeyID::DELETE,        ControlMask,    "\033[M",       -1,    0},
	{ KeyID::DELETE,        ControlMask,    "\033[3;5~",    +1,    0},
	{ KeyID::DELETE,        ShiftMask,      "\033[2K",      -1,    0},
	{ KeyID::DELETE,        ShiftMask,      "\033[3;2~",    +1,    0},
	{ KeyID::DELETE,        XK_ANY_MOD,     "\033[P",       -1,    0},
	{ KeyID::DELETE,        XK_ANY_MOD,     "\033[3~",      +1,    0},
	{ KeyID::BACKSPACE,     XK_NO_MOD,      "\177",          0,    0},
	{ KeyID::BACKSPACE,     Mod1Mask,       "\033\177",      0,    0},
	{ KeyID::HOME,          ShiftMask,      "\033[2J",       0,   -1},
	{ KeyID::HOME,          ShiftMask,      "\033[1;2H",     0,   +1},
	{ KeyID::HOME,          XK_ANY_MOD,     "\033[H",        0,   -1},
	{ KeyID::HOME,          XK_ANY_MOD,     "\033[1~",       0,   +1},
	{ KeyID::END,           ControlMask,    "\033[J",       -1,    0},
	{ KeyID::END,           ControlMask,    "\033[1;5F",    +1,    0},
	{ KeyID::END,           ShiftMask,      "\033[K",       -1,    0},
	{ KeyID::END,           ShiftMask,      "\033[1;2F",    +1,    0},
	{ KeyID::END,           XK_ANY_MOD,     "\033[4~",       0,    0},
	{ KeyID::PRIOR,         ControlMask,    "\033[5;5~",     0,    0},
	{ KeyID::PRIOR,         ShiftMask,      "\033[5;2~",     0,    0},
	{ KeyID::PRIOR,         XK_ANY_MOD,     "\033[5~",       0,    0},
	{ KeyID::NEXT,          ControlMask,    "\033[6;5~",     0,    0},
	{ KeyID::NEXT,          ShiftMask,      "\033[6;2~",     0,    0},
	{ KeyID::NEXT,          XK_ANY_MOD,     "\033[6~",       0,    0},
	{ KeyID::F1,            XK_NO_MOD,      "\033OP" ,       0,    0},
	{ KeyID::F1, /* F13 */  ShiftMask,      "\033[1;2P",     0,    0},
	{ KeyID::F1, /* F25 */  ControlMask,    "\033[1;5P",     0,    0},
	{ KeyID::F1, /* F37 */  Mod4Mask,       "\033[1;6P",     0,    0},
	{ KeyID::F1, /* F49 */  Mod1Mask,       "\033[1;3P",     0,    0},
	{ KeyID::F1, /* F61 */  Mod3Mask,       "\033[1;4P",     0,    0},
	{ KeyID::F2,            XK_NO_MOD,      "\033OQ" ,       0,    0},
	{ KeyID::F2, /* F14 */  ShiftMask,      "\033[1;2Q",     0,    0},
	{ KeyID::F2, /* F26 */  ControlMask,    "\033[1;5Q",     0,    0},
	{ KeyID::F2, /* F38 */  Mod4Mask,       "\033[1;6Q",     0,    0},
	{ KeyID::F2, /* F50 */  Mod1Mask,       "\033[1;3Q",     0,    0},
	{ KeyID::F2, /* F62 */  Mod3Mask,       "\033[1;4Q",     0,    0},
	{ KeyID::F3,            XK_NO_MOD,      "\033OR" ,       0,    0},
	{ KeyID::F3, /* F15 */  ShiftMask,      "\033[1;2R",     0,    0},
	{ KeyID::F3, /* F27 */  ControlMask,    "\033[1;5R",     0,    0},
	{ KeyID::F3, /* F39 */  Mod4Mask,       "\033[1;6R",     0,    0},
	{ KeyID::F3, /* F51 */  Mod1Mask,       "\033[1;3R",     0,    0},
	{ KeyID::F3, /* F63 */  Mod3Mask,       "\033[1;4R",     0,    0},
	{ KeyID::F4,            XK_NO_MOD,      "\033OS" ,       0,    0},
	{ KeyID::F4, /* F16 */  ShiftMask,      "\033[1;2S",     0,    0},
	{ KeyID::F4, /* F28 */  ControlMask,    "\033[1;5S",     0,    0},
	{ KeyID::F4, /* F40 */  Mod4Mask,       "\033[1;6S",     0,    0},
	{ KeyID::F4, /* F52 */  Mod1Mask,       "\033[1;3S",     0,    0},
	{ KeyID::F5,            XK_NO_MOD,      "\033[15~",      0,    0},
	{ KeyID::F5, /* F17 */  ShiftMask,      "\033[15;2~",    0,    0},
	{ KeyID::F5, /* F29 */  ControlMask,    "\033[15;5~",    0,    0},
	{ KeyID::F5, /* F41 */  Mod4Mask,       "\033[15;6~",    0,    0},
	{ KeyID::F5, /* F53 */  Mod1Mask,       "\033[15;3~",    0,    0},
	{ KeyID::F6,            XK_NO_MOD,      "\033[17~",      0,    0},
	{ KeyID::F6, /* F18 */  ShiftMask,      "\033[17;2~",    0,    0},
	{ KeyID::F6, /* F30 */  ControlMask,    "\033[17;5~",    0,    0},
	{ KeyID::F6, /* F42 */  Mod4Mask,       "\033[17;6~",    0,    0},
	{ KeyID::F6, /* F54 */  Mod1Mask,       "\033[17;3~",    0,    0},
	{ KeyID::F7,            XK_NO_MOD,      "\033[18~",      0,    0},
	{ KeyID::F7, /* F19 */  ShiftMask,      "\033[18;2~",    0,    0},
	{ KeyID::F7, /* F31 */  ControlMask,    "\033[18;5~",    0,    0},
	{ KeyID::F7, /* F43 */  Mod4Mask,       "\033[18;6~",    0,    0},
	{ KeyID::F7, /* F55 */  Mod1Mask,       "\033[18;3~",    0,    0},
	{ KeyID::F8,            XK_NO_MOD,      "\033[19~",      0,    0},
	{ KeyID::F8, /* F20 */  ShiftMask,      "\033[19;2~",    0,    0},
	{ KeyID::F8, /* F32 */  ControlMask,    "\033[19;5~",    0,    0},
	{ KeyID::F8, /* F44 */  Mod4Mask,       "\033[19;6~",    0,    0},
	{ KeyID::F8, /* F56 */  Mod1Mask,       "\033[19;3~",    0,    0},
	{ KeyID::F9,            XK_NO_MOD,      "\033[20~",      0,    0},
	{ KeyID::F9, /* F21 */  ShiftMask,      "\033[20;2~",    0,    0},
	{ KeyID::F9, /* F33 */  ControlMask,    "\033[20;5~",    0,    0},
	{ KeyID::F9, /* F45 */  Mod4Mask,       "\033[20;6~",    0,    0},
	{ KeyID::F9, /* F57 */  Mod1Mask,       "\033[20;3~",    0,    0},
	{ KeyID::F10,           XK_NO_MOD,      "\033[21~",      0,    0},
	{ KeyID::F10, /* F22 */ ShiftMask,      "\033[21;2~",    0,    0},
	{ KeyID::F10, /* F34 */ ControlMask,    "\033[21;5~",    0,    0},
	{ KeyID::F10, /* F46 */ Mod4Mask,       "\033[21;6~",    0,    0},
	{ KeyID::F10, /* F58 */ Mod1Mask,       "\033[21;3~",    0,    0},
	{ KeyID::F11,           XK_NO_MOD,      "\033[23~",      0,    0},
	{ KeyID::F11, /* F23 */ ShiftMask,      "\033[23;2~",    0,    0},
	{ KeyID::F11, /* F35 */ ControlMask,    "\033[23;5~",    0,    0},
	{ KeyID::F11, /* F47 */ Mod4Mask,       "\033[23;6~",    0,    0},
	{ KeyID::F11, /* F59 */ Mod1Mask,       "\033[23;3~",    0,    0},
	{ KeyID::F12,           XK_NO_MOD,      "\033[24~",      0,    0},
	{ KeyID::F12, /* F24 */ ShiftMask,      "\033[24;2~",    0,    0},
	{ KeyID::F12, /* F36 */ ControlMask,    "\033[24;5~",    0,    0},
	{ KeyID::F12, /* F48 */ Mod4Mask,       "\033[24;6~",    0,    0},
	{ KeyID::F12, /* F60 */ Mod1Mask,       "\033[24;3~",    0,    0},
	{ KeyID::F13,           XK_NO_MOD,      "\033[1;2P",     0,    0},
	{ KeyID::F14,           XK_NO_MOD,      "\033[1;2Q",     0,    0},
	{ KeyID::F15,           XK_NO_MOD,      "\033[1;2R",     0,    0},
	{ KeyID::F16,           XK_NO_MOD,      "\033[1;2S",     0,    0},
	{ KeyID::F17,           XK_NO_MOD,      "\033[15;2~",    0,    0},
	{ KeyID::F18,           XK_NO_MOD,      "\033[17;2~",    0,    0},
	{ KeyID::F19,           XK_NO_MOD,      "\033[18;2~",    0,    0},
	{ KeyID::F20,           XK_NO_MOD,      "\033[19;2~",    0,    0},
	{ KeyID::F21,           XK_NO_MOD,      "\033[20;2~",    0,    0},
	{ KeyID::F22,           XK_NO_MOD,      "\033[21;2~",    0,    0},
	{ KeyID::F23,           XK_NO_MOD,      "\033[23;2~",    0,    0},
	{ KeyID::F24,           XK_NO_MOD,      "\033[24;2~",    0,    0},
	{ KeyID::F25,           XK_NO_MOD,      "\033[1;5P",     0,    0},
	{ KeyID::F26,           XK_NO_MOD,      "\033[1;5Q",     0,    0},
	{ KeyID::F27,           XK_NO_MOD,      "\033[1;5R",     0,    0},
	{ KeyID::F28,           XK_NO_MOD,      "\033[1;5S",     0,    0},
	{ KeyID::F29,           XK_NO_MOD,      "\033[15;5~",    0,    0},
	{ KeyID::F30,           XK_NO_MOD,      "\033[17;5~",    0,    0},
	{ KeyID::F31,           XK_NO_MOD,      "\033[18;5~",    0,    0},
	{ KeyID::F32,           XK_NO_MOD,      "\033[19;5~",    0,    0},
	{ KeyID::F33,           XK_NO_MOD,      "\033[20;5~",    0,    0},
	{ KeyID::F34,           XK_NO_MOD,      "\033[21;5~",    0,    0},
	{ KeyID::F35,           XK_NO_MOD,      "\033[23;5~",    0,    0},
}};

/*
 * Printable characters in ASCII, used to estimate the advance width
 * of single wide characters.
 */
constexpr std::string_view ASCII_PRINTABLE{
	" !\"#$%&'()*+,-./0123456789:;<=>?"
	"@ABCDEFGHIJKLMNOPQRSTUVWXYZ[\\]^_"
	"`abcdefghijklmnopqrstuvwxyz{|}~"
};

/*
 * Selection types' masks.
 * Use the same masks as usual.
 * Button1Mask is always unset, to make masks match between ButtonPress.
 * ButtonRelease and MotionNotify.
 * If no match is found, regular selection is used.
 */
constexpr std::array<std::pair<Selection::Type, xpp::InputMask>, 2> SEL_MASKS = {
	std::pair{Selection::Type::REGULAR,     xpp::InputMask{}},
	         {Selection::Type::RECTANGULAR, xpp::InputMask{xpp::InputModifier::MOD1}}
};

// see implementation file
std::vector<MouseShortcut> get_mouse_shortcuts(Nst &nst);

// see implementation file
std::vector<KbdShortcut> get_kbd_shortcuts(Nst &nst);

}} // end ns nst::config

#endif // inc. guard
