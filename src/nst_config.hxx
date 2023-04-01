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

using KeyID = xpp::KeySymID;
using Mod = xpp::InputModifier;
using Mask = xpp::InputMask;
using AppKey = Key::AppKeypad;

/*
 * State bits to ignore when matching key or button events.  By default,
 * numlock (MOD2) and keyboard layout (XKB_GROUP_INDEX) are ignored.
 */
inline constexpr xpp::InputMask IGNOREMOD{Mod::MOD2, Mod::XKB_GROUP_INDEX};

/*
 * Special keys (change & recompile st.info accordingly)
 *
 * Mask value:
 * * Use Mod::ANY to match the key no matter modifiers state
 * * Use Mod::NONE to match the key alone (no modifiers)
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
 * this table sequentially, so any Mod::ANY must be in the last
 * position for a key.
 */

/*
 * If you want keys other than the X11 function keys (0xFD00 - 0xFFFF)
 * to be mapped below, add them to this array.
 */
const std::set<xpp::KeySymID> MAPPED_KEYS{
};

// we use a multiset for the key definitions below
// the keysym is the comparison key, so we don't have to iterate over the
// complete list of keys linearly, but only over a small list of key
// combinations that share the same keysym.
struct KeyCmp {
	bool operator()(const Key &a, const Key &b) const {
		return a.k < b.k;
	}
};

/*
 * This is the huge list of keys which defines all compatibility to the Linux
 * world. Please decide about changes wisely.
 */
const std::multiset<Key, KeyCmp> KEYS{{
	// keysym               mask                                       string          appkey             appcursor
	{ KeyID::KP_HOME,       Mask{Mod::SHIFT},                          "\033[2J",      AppKey::IGNORE,   -1},
	{ KeyID::KP_HOME,       Mask{Mod::SHIFT},                          "\033[1;2H",    AppKey::IGNORE,   +1},
	{ KeyID::KP_HOME,       Mask{Mod::ANY},                            "\033[H",       AppKey::IGNORE,   -1},
	{ KeyID::KP_HOME,       Mask{Mod::ANY},                            "\033[1~",      AppKey::IGNORE,   +1},
	{ KeyID::KP_UP,         Mask{Mod::ANY},                            "\033Ox",       AppKey::ENABLED,   0},
	{ KeyID::KP_UP,         Mask{Mod::ANY},                            "\033[A",       AppKey::IGNORE,   -1},
	{ KeyID::KP_UP,         Mask{Mod::ANY},                            "\033OA",       AppKey::IGNORE,   +1},
	{ KeyID::KP_DOWN,       Mask{Mod::ANY},                            "\033Or",       AppKey::ENABLED,   0},
	{ KeyID::KP_DOWN,       Mask{Mod::ANY},                            "\033[B",       AppKey::IGNORE,   -1},
	{ KeyID::KP_DOWN,       Mask{Mod::ANY},                            "\033OB",       AppKey::IGNORE,   +1},
	{ KeyID::KP_LEFT,       Mask{Mod::ANY},                            "\033Ot",       AppKey::ENABLED,   0},
	{ KeyID::KP_LEFT,       Mask{Mod::ANY},                            "\033[D",       AppKey::IGNORE,   -1},
	{ KeyID::KP_LEFT,       Mask{Mod::ANY},                            "\033OD",       AppKey::IGNORE,   +1},
	{ KeyID::KP_RIGHT,      Mask{Mod::ANY},                            "\033Ov",       AppKey::ENABLED,   0},
	{ KeyID::KP_RIGHT,      Mask{Mod::ANY},                            "\033[C",       AppKey::IGNORE,   -1},
	{ KeyID::KP_RIGHT,      Mask{Mod::ANY},                            "\033OC",       AppKey::IGNORE,   +1},
	{ KeyID::KP_PRIOR,      Mask{Mod::SHIFT},                          "\033[5;2~",    AppKey::IGNORE,    0},
	{ KeyID::KP_PRIOR,      Mask{Mod::ANY},                            "\033[5~",      AppKey::IGNORE,    0},
	{ KeyID::KP_BEGIN,      Mask{Mod::ANY},                            "\033[E",       AppKey::IGNORE,    0},
	{ KeyID::KP_END,        Mask{Mod::CONTROL},                        "\033[J",       AppKey::DISABLED,  0},
	{ KeyID::KP_END,        Mask{Mod::CONTROL},                        "\033[1;5F",    AppKey::ENABLED,   0},
	{ KeyID::KP_END,        Mask{Mod::SHIFT},                          "\033[K",       AppKey::DISABLED,  0},
	{ KeyID::KP_END,        Mask{Mod::SHIFT},                          "\033[1;2F",    AppKey::ENABLED,   0},
	{ KeyID::KP_END,        Mask{Mod::ANY},                            "\033[4~",      AppKey::IGNORE,    0},
	{ KeyID::KP_NEXT,       Mask{Mod::SHIFT},                          "\033[6;2~",    AppKey::IGNORE,    0},
	{ KeyID::KP_NEXT,       Mask{Mod::ANY},                            "\033[6~",      AppKey::IGNORE,    0},
	{ KeyID::KP_INSERT,     Mask{Mod::SHIFT},                          "\033[2;2~",    AppKey::ENABLED,   0},
	{ KeyID::KP_INSERT,     Mask{Mod::SHIFT},                          "\033[4l",      AppKey::DISABLED,  0},
	{ KeyID::KP_INSERT,     Mask{Mod::CONTROL},                        "\033[L",       AppKey::DISABLED,  0},
	{ KeyID::KP_INSERT,     Mask{Mod::CONTROL},                        "\033[2;5~",    AppKey::ENABLED,   0},
	{ KeyID::KP_INSERT,     Mask{Mod::ANY},                            "\033[4h",      AppKey::DISABLED,  0},
	{ KeyID::KP_INSERT,     Mask{Mod::ANY},                            "\033[2~",      AppKey::ENABLED,   0},
	{ KeyID::KP_DELETE,     Mask{Mod::CONTROL},                        "\033[M",       AppKey::DISABLED,  0},
	{ KeyID::KP_DELETE,     Mask{Mod::CONTROL},                        "\033[3;5~",    AppKey::ENABLED,   0},
	{ KeyID::KP_DELETE,     Mask{Mod::SHIFT},                          "\033[2K",      AppKey::DISABLED,  0},
	{ KeyID::KP_DELETE,     Mask{Mod::SHIFT},                          "\033[3;2~",    AppKey::ENABLED,   0},
	{ KeyID::KP_DELETE,     Mask{Mod::ANY},                            "\033[P",       AppKey::DISABLED,  0},
	{ KeyID::KP_DELETE,     Mask{Mod::ANY},                            "\033[3~",      AppKey::ENABLED,   0},
	{ KeyID::KP_MULTIPLY,   Mask{Mod::ANY},                            "\033Oj",       AppKey::NO_NUMLOCK,0},
	{ KeyID::KP_ADD,        Mask{Mod::ANY},                            "\033Ok",       AppKey::NO_NUMLOCK,0},
	{ KeyID::KP_ENTER,      Mask{Mod::ANY},                            "\033OM",       AppKey::NO_NUMLOCK,0},
	{ KeyID::KP_ENTER,      Mask{Mod::ANY},                            "\r",           AppKey::DISABLED,  0},
	{ KeyID::KP_SUBTRACT,   Mask{Mod::ANY},                            "\033Om",       AppKey::NO_NUMLOCK,0},
	{ KeyID::KP_DECIMAL,    Mask{Mod::ANY},                            "\033On",       AppKey::NO_NUMLOCK,0},
	{ KeyID::KP_DIVIDE,     Mask{Mod::ANY},                            "\033Oo",       AppKey::NO_NUMLOCK,0},
	{ KeyID::KP_0,          Mask{Mod::ANY},                            "\033Op",       AppKey::NO_NUMLOCK,0},
	{ KeyID::KP_1,          Mask{Mod::ANY},                            "\033Oq",       AppKey::NO_NUMLOCK,0},
	{ KeyID::KP_2,          Mask{Mod::ANY},                            "\033Or",       AppKey::NO_NUMLOCK,0},
	{ KeyID::KP_3,          Mask{Mod::ANY},                            "\033Os",       AppKey::NO_NUMLOCK,0},
	{ KeyID::KP_4,          Mask{Mod::ANY},                            "\033Ot",       AppKey::NO_NUMLOCK,0},
	{ KeyID::KP_5,          Mask{Mod::ANY},                            "\033Ou",       AppKey::NO_NUMLOCK,0},
	{ KeyID::KP_6,          Mask{Mod::ANY},                            "\033Ov",       AppKey::NO_NUMLOCK,0},
	{ KeyID::KP_7,          Mask{Mod::ANY},                            "\033Ow",       AppKey::NO_NUMLOCK,0},
	{ KeyID::KP_8,          Mask{Mod::ANY},                            "\033Ox",       AppKey::NO_NUMLOCK,0},
	{ KeyID::KP_9,          Mask{Mod::ANY},                            "\033Oy",       AppKey::NO_NUMLOCK,0},
	{ KeyID::UP,            Mask{Mod::SHIFT},                          "\033[1;2A",    AppKey::IGNORE,    0},
	{ KeyID::UP,            Mask{Mod::MOD1},                           "\033[1;3A",    AppKey::IGNORE,    0},
	{ KeyID::UP,            Mask{Mod::SHIFT, Mod::MOD1},               "\033[1;4A",    AppKey::IGNORE,    0},
	{ KeyID::UP,            Mask{Mod::CONTROL},                        "\033[1;5A",    AppKey::IGNORE,    0},
	{ KeyID::UP,            Mask{Mod::SHIFT, Mod::CONTROL},            "\033[1;6A",    AppKey::IGNORE,    0},
	{ KeyID::UP,            Mask{Mod::CONTROL, Mod::MOD1},             "\033[1;7A",    AppKey::IGNORE,    0},
	{ KeyID::UP,            Mask{Mod::SHIFT, Mod::CONTROL, Mod::MOD1}, "\033[1;8A",    AppKey::IGNORE,    0},
	{ KeyID::UP,            Mask{Mod::ANY},                            "\033[A",       AppKey::IGNORE,   -1},
	{ KeyID::UP,            Mask{Mod::ANY},                            "\033OA",       AppKey::IGNORE,   +1},
	{ KeyID::DOWN,          Mask{Mod::SHIFT},                          "\033[1;2B",    AppKey::IGNORE,    0},
	{ KeyID::DOWN,          Mask{Mod::MOD1},                           "\033[1;3B",    AppKey::IGNORE,    0},
	{ KeyID::DOWN,          Mask{Mod::SHIFT, Mod::MOD1},               "\033[1;4B",    AppKey::IGNORE,    0},
	{ KeyID::DOWN,          Mask{Mod::CONTROL},                        "\033[1;5B",    AppKey::IGNORE,    0},
	{ KeyID::DOWN,          Mask{Mod::SHIFT, Mod::CONTROL},            "\033[1;6B",    AppKey::IGNORE,    0},
	{ KeyID::DOWN,          Mask{Mod::CONTROL, Mod::MOD1},             "\033[1;7B",    AppKey::IGNORE,    0},
	{ KeyID::DOWN,          Mask{Mod::SHIFT, Mod::CONTROL, Mod::MOD1}, "\033[1;8B",    AppKey::IGNORE,    0},
	{ KeyID::DOWN,          Mask{Mod::ANY},                            "\033[B",       AppKey::IGNORE,   -1},
	{ KeyID::DOWN,          Mask{Mod::ANY},                            "\033OB",       AppKey::IGNORE,   +1},
	{ KeyID::LEFT,          Mask{Mod::SHIFT},                          "\033[1;2D",    AppKey::IGNORE,    0},
	{ KeyID::LEFT,          Mask{Mod::MOD1},                           "\033[1;3D",    AppKey::IGNORE,    0},
	{ KeyID::LEFT,          Mask{Mod::SHIFT, Mod::MOD1},               "\033[1;4D",    AppKey::IGNORE,    0},
	{ KeyID::LEFT,          Mask{Mod::CONTROL},                        "\033[1;5D",    AppKey::IGNORE,    0},
	{ KeyID::LEFT,          Mask{Mod::SHIFT, Mod::CONTROL},            "\033[1;6D",    AppKey::IGNORE,    0},
	{ KeyID::LEFT,          Mask{Mod::CONTROL,Mod::MOD1},              "\033[1;7D",    AppKey::IGNORE,    0},
	{ KeyID::LEFT,          Mask{Mod::SHIFT,Mod::CONTROL,Mod::MOD1},   "\033[1;8D",    AppKey::IGNORE,    0},
	{ KeyID::LEFT,          Mask{Mod::ANY},                            "\033[D",       AppKey::IGNORE,   -1},
	{ KeyID::LEFT,          Mask{Mod::ANY},                            "\033OD",       AppKey::IGNORE,   +1},
	{ KeyID::RIGHT,         Mask{Mod::SHIFT},                          "\033[1;2C",    AppKey::IGNORE,    0},
	{ KeyID::RIGHT,         Mask{Mod::MOD1},                           "\033[1;3C",    AppKey::IGNORE,    0},
	{ KeyID::RIGHT,         Mask{Mod::SHIFT,Mod::MOD1},                "\033[1;4C",    AppKey::IGNORE,    0},
	{ KeyID::RIGHT,         Mask{Mod::CONTROL},                        "\033[1;5C",    AppKey::IGNORE,    0},
	{ KeyID::RIGHT,         Mask{Mod::SHIFT,Mod::CONTROL},             "\033[1;6C",    AppKey::IGNORE,    0},
	{ KeyID::RIGHT,         Mask{Mod::CONTROL,Mod::MOD1},              "\033[1;7C",    AppKey::IGNORE,    0},
	{ KeyID::RIGHT,         Mask{Mod::SHIFT,Mod::CONTROL,Mod::MOD1},   "\033[1;8C",    AppKey::IGNORE,    0},
	{ KeyID::RIGHT,         Mask{Mod::ANY},                            "\033[C",       AppKey::IGNORE,   -1},
	{ KeyID::RIGHT,         Mask{Mod::ANY},                            "\033OC",       AppKey::IGNORE,   +1},
	{ KeyID::ISO_LEFT_TAB,  Mask{Mod::SHIFT},                          "\033[Z",       AppKey::IGNORE,    0},
	{ KeyID::RETURN,        Mask{Mod::MOD1},                           "\033\r",       AppKey::IGNORE,    0},
	{ KeyID::RETURN,        Mask{Mod::ANY},                            "\r",           AppKey::IGNORE,    0},
	{ KeyID::INSERT,        Mask{Mod::SHIFT},                          "\033[4l",      AppKey::DISABLED,  0},
	{ KeyID::INSERT,        Mask{Mod::SHIFT},                          "\033[2;2~",    AppKey::ENABLED,   0},
	{ KeyID::INSERT,        Mask{Mod::CONTROL},                        "\033[L",       AppKey::DISABLED,  0},
	{ KeyID::INSERT,        Mask{Mod::CONTROL},                        "\033[2;5~",    AppKey::ENABLED,   0},
	{ KeyID::INSERT,        Mask{Mod::ANY},                            "\033[4h",      AppKey::DISABLED,  0},
	{ KeyID::INSERT,        Mask{Mod::ANY},                            "\033[2~",      AppKey::ENABLED,   0},
	{ KeyID::DELETE,        Mask{Mod::CONTROL},                        "\033[M",       AppKey::DISABLED,  0},
	{ KeyID::DELETE,        Mask{Mod::CONTROL},                        "\033[3;5~",    AppKey::ENABLED,   0},
	{ KeyID::DELETE,        Mask{Mod::SHIFT},                          "\033[2K",      AppKey::DISABLED,  0},
	{ KeyID::DELETE,        Mask{Mod::SHIFT},                          "\033[3;2~",    AppKey::ENABLED,   0},
	{ KeyID::DELETE,        Mask{Mod::ANY},                            "\033[P",       AppKey::DISABLED,  0},
	{ KeyID::DELETE,        Mask{Mod::ANY},                            "\033[3~",      AppKey::ENABLED,   0},
	{ KeyID::BACKSPACE,     Mask{Mod::NONE},                           "\177",         AppKey::IGNORE,    0},
	{ KeyID::BACKSPACE,     Mask{Mod::MOD1},                           "\033\177",     AppKey::IGNORE,    0},
	{ KeyID::HOME,          Mask{Mod::SHIFT},                          "\033[2J",      AppKey::IGNORE,   -1},
	{ KeyID::HOME,          Mask{Mod::SHIFT},                          "\033[1;2H",    AppKey::IGNORE,   +1},
	{ KeyID::HOME,          Mask{Mod::ANY},                            "\033[H",       AppKey::IGNORE,   -1},
	{ KeyID::HOME,          Mask{Mod::ANY},                            "\033[1~",      AppKey::IGNORE,   +1},
	{ KeyID::END,           Mask{Mod::CONTROL},                        "\033[J",       AppKey::DISABLED,  0},
	{ KeyID::END,           Mask{Mod::CONTROL},                        "\033[1;5F",    AppKey::ENABLED,   0},
	{ KeyID::END,           Mask{Mod::SHIFT},                          "\033[K",       AppKey::DISABLED,  0},
	{ KeyID::END,           Mask{Mod::SHIFT},                          "\033[1;2F",    AppKey::ENABLED,   0},
	{ KeyID::END,           Mask{Mod::ANY},                            "\033[4~",      AppKey::IGNORE,    0},
	{ KeyID::PRIOR,         Mask{Mod::CONTROL},                        "\033[5;5~",    AppKey::IGNORE,    0},
	{ KeyID::PRIOR,         Mask{Mod::SHIFT},                          "\033[5;2~",    AppKey::IGNORE,    0},
	{ KeyID::PRIOR,         Mask{Mod::ANY},                            "\033[5~",      AppKey::IGNORE,    0},
	{ KeyID::NEXT,          Mask{Mod::CONTROL},                        "\033[6;5~",    AppKey::IGNORE,    0},
	{ KeyID::NEXT,          Mask{Mod::SHIFT},                          "\033[6;2~",    AppKey::IGNORE,    0},
	{ KeyID::NEXT,          Mask{Mod::ANY},                            "\033[6~",      AppKey::IGNORE,    0},
	{ KeyID::F1,            Mask{Mod::NONE},                           "\033OP" ,      AppKey::IGNORE,    0},
	{ KeyID::F1, /* F13 */  Mask{Mod::SHIFT},                          "\033[1;2P",    AppKey::IGNORE,    0},
	{ KeyID::F1, /* F25 */  Mask{Mod::CONTROL},                        "\033[1;5P",    AppKey::IGNORE,    0},
	{ KeyID::F1, /* F37 */  Mask{Mod::MOD4},                           "\033[1;6P",    AppKey::IGNORE,    0},
	{ KeyID::F1, /* F49 */  Mask{Mod::MOD1},                           "\033[1;3P",    AppKey::IGNORE,    0},
	{ KeyID::F1, /* F61 */  Mask{Mod::MOD3},                           "\033[1;4P",    AppKey::IGNORE,    0},
	{ KeyID::F2,            Mask{Mod::NONE},                           "\033OQ" ,      AppKey::IGNORE,    0},
	{ KeyID::F2, /* F14 */  Mask{Mod::SHIFT},                          "\033[1;2Q",    AppKey::IGNORE,    0},
	{ KeyID::F2, /* F26 */  Mask{Mod::CONTROL},                        "\033[1;5Q",    AppKey::IGNORE,    0},
	{ KeyID::F2, /* F38 */  Mask{Mod::MOD4},                           "\033[1;6Q",    AppKey::IGNORE,    0},
	{ KeyID::F2, /* F50 */  Mask{Mod::MOD1},                           "\033[1;3Q",    AppKey::IGNORE,    0},
	{ KeyID::F2, /* F62 */  Mask{Mod::MOD3},                           "\033[1;4Q",    AppKey::IGNORE,    0},
	{ KeyID::F3,            Mask{Mod::NONE},                           "\033OR" ,      AppKey::IGNORE,    0},
	{ KeyID::F3, /* F15 */  Mask{Mod::SHIFT},                          "\033[1;2R",    AppKey::IGNORE,    0},
	{ KeyID::F3, /* F27 */  Mask{Mod::CONTROL},                        "\033[1;5R",    AppKey::IGNORE,    0},
	{ KeyID::F3, /* F39 */  Mask{Mod::MOD4},                           "\033[1;6R",    AppKey::IGNORE,    0},
	{ KeyID::F3, /* F51 */  Mask{Mod::MOD1},                           "\033[1;3R",    AppKey::IGNORE,    0},
	{ KeyID::F3, /* F63 */  Mask{Mod::MOD3},                           "\033[1;4R",    AppKey::IGNORE,    0},
	{ KeyID::F4,            Mask{Mod::NONE},                           "\033OS" ,      AppKey::IGNORE,    0},
	{ KeyID::F4, /* F16 */  Mask{Mod::SHIFT},                          "\033[1;2S",    AppKey::IGNORE,    0},
	{ KeyID::F4, /* F28 */  Mask{Mod::CONTROL},                        "\033[1;5S",    AppKey::IGNORE,    0},
	{ KeyID::F4, /* F40 */  Mask{Mod::MOD4},                           "\033[1;6S",    AppKey::IGNORE,    0},
	{ KeyID::F4, /* F52 */  Mask{Mod::MOD1},                           "\033[1;3S",    AppKey::IGNORE,    0},
	{ KeyID::F5,            Mask{Mod::NONE},                           "\033[15~",     AppKey::IGNORE,    0},
	{ KeyID::F5, /* F17 */  Mask{Mod::SHIFT},                          "\033[15;2~",   AppKey::IGNORE,    0},
	{ KeyID::F5, /* F29 */  Mask{Mod::CONTROL},                        "\033[15;5~",   AppKey::IGNORE,    0},
	{ KeyID::F5, /* F41 */  Mask{Mod::MOD4},                           "\033[15;6~",   AppKey::IGNORE,    0},
	{ KeyID::F5, /* F53 */  Mask{Mod::MOD1},                           "\033[15;3~",   AppKey::IGNORE,    0},
	{ KeyID::F6,            Mask{Mod::NONE},                           "\033[17~",     AppKey::IGNORE,    0},
	{ KeyID::F6, /* F18 */  Mask{Mod::SHIFT},                          "\033[17;2~",   AppKey::IGNORE,    0},
	{ KeyID::F6, /* F30 */  Mask{Mod::CONTROL},                        "\033[17;5~",   AppKey::IGNORE,    0},
	{ KeyID::F6, /* F42 */  Mask{Mod::MOD4},                           "\033[17;6~",   AppKey::IGNORE,    0},
	{ KeyID::F6, /* F54 */  Mask{Mod::MOD1},                           "\033[17;3~",   AppKey::IGNORE,    0},
	{ KeyID::F7,            Mask{Mod::NONE},                           "\033[18~",     AppKey::IGNORE,    0},
	{ KeyID::F7, /* F19 */  Mask{Mod::SHIFT},                          "\033[18;2~",   AppKey::IGNORE,    0},
	{ KeyID::F7, /* F31 */  Mask{Mod::CONTROL},                        "\033[18;5~",   AppKey::IGNORE,    0},
	{ KeyID::F7, /* F43 */  Mask{Mod::MOD4},                           "\033[18;6~",   AppKey::IGNORE,    0},
	{ KeyID::F7, /* F55 */  Mask{Mod::MOD1},                           "\033[18;3~",   AppKey::IGNORE,    0},
	{ KeyID::F8,            Mask{Mod::NONE},                           "\033[19~",     AppKey::IGNORE,    0},
	{ KeyID::F8, /* F20 */  Mask{Mod::SHIFT},                          "\033[19;2~",   AppKey::IGNORE,    0},
	{ KeyID::F8, /* F32 */  Mask{Mod::CONTROL},                        "\033[19;5~",   AppKey::IGNORE,    0},
	{ KeyID::F8, /* F44 */  Mask{Mod::MOD4},                           "\033[19;6~",   AppKey::IGNORE,    0},
	{ KeyID::F8, /* F56 */  Mask{Mod::MOD1},                           "\033[19;3~",   AppKey::IGNORE,    0},
	{ KeyID::F9,            Mask{Mod::NONE},                           "\033[20~",     AppKey::IGNORE,    0},
	{ KeyID::F9, /* F21 */  Mask{Mod::SHIFT},                          "\033[20;2~",   AppKey::IGNORE,    0},
	{ KeyID::F9, /* F33 */  Mask{Mod::CONTROL},                        "\033[20;5~",   AppKey::IGNORE,    0},
	{ KeyID::F9, /* F45 */  Mask{Mod::MOD4},                           "\033[20;6~",   AppKey::IGNORE,    0},
	{ KeyID::F9, /* F57 */  Mask{Mod::MOD1},                           "\033[20;3~",   AppKey::IGNORE,    0},
	{ KeyID::F10,           Mask{Mod::NONE},                           "\033[21~",     AppKey::IGNORE,    0},
	{ KeyID::F10, /* F22 */ Mask{Mod::SHIFT},                          "\033[21;2~",   AppKey::IGNORE,    0},
	{ KeyID::F10, /* F34 */ Mask{Mod::CONTROL},                        "\033[21;5~",   AppKey::IGNORE,    0},
	{ KeyID::F10, /* F46 */ Mask{Mod::MOD4},                           "\033[21;6~",   AppKey::IGNORE,    0},
	{ KeyID::F10, /* F58 */ Mask{Mod::MOD1},                           "\033[21;3~",   AppKey::IGNORE,    0},
	{ KeyID::F11,           Mask{Mod::NONE},                           "\033[23~",     AppKey::IGNORE,    0},
	{ KeyID::F11, /* F23 */ Mask{Mod::SHIFT},                          "\033[23;2~",   AppKey::IGNORE,    0},
	{ KeyID::F11, /* F35 */ Mask{Mod::CONTROL},                        "\033[23;5~",   AppKey::IGNORE,    0},
	{ KeyID::F11, /* F47 */ Mask{Mod::MOD4},                           "\033[23;6~",   AppKey::IGNORE,    0},
	{ KeyID::F11, /* F59 */ Mask{Mod::MOD1},                           "\033[23;3~",   AppKey::IGNORE,    0},
	{ KeyID::F12,           Mask{Mod::NONE},                           "\033[24~",     AppKey::IGNORE,    0},
	{ KeyID::F12, /* F24 */ Mask{Mod::SHIFT},                          "\033[24;2~",   AppKey::IGNORE,    0},
	{ KeyID::F12, /* F36 */ Mask{Mod::CONTROL},                        "\033[24;5~",   AppKey::IGNORE,    0},
	{ KeyID::F12, /* F48 */ Mask{Mod::MOD4},                           "\033[24;6~",   AppKey::IGNORE,    0},
	{ KeyID::F12, /* F60 */ Mask{Mod::MOD1},                           "\033[24;3~",   AppKey::IGNORE,    0},
	{ KeyID::F13,           Mask{Mod::NONE},                           "\033[1;2P",    AppKey::IGNORE,    0},
	{ KeyID::F14,           Mask{Mod::NONE},                           "\033[1;2Q",    AppKey::IGNORE,    0},
	{ KeyID::F15,           Mask{Mod::NONE},                           "\033[1;2R",    AppKey::IGNORE,    0},
	{ KeyID::F16,           Mask{Mod::NONE},                           "\033[1;2S",    AppKey::IGNORE,    0},
	{ KeyID::F17,           Mask{Mod::NONE},                           "\033[15;2~",   AppKey::IGNORE,    0},
	{ KeyID::F18,           Mask{Mod::NONE},                           "\033[17;2~",   AppKey::IGNORE,    0},
	{ KeyID::F19,           Mask{Mod::NONE},                           "\033[18;2~",   AppKey::IGNORE,    0},
	{ KeyID::F20,           Mask{Mod::NONE},                           "\033[19;2~",   AppKey::IGNORE,    0},
	{ KeyID::F21,           Mask{Mod::NONE},                           "\033[20;2~",   AppKey::IGNORE,    0},
	{ KeyID::F22,           Mask{Mod::NONE},                           "\033[21;2~",   AppKey::IGNORE,    0},
	{ KeyID::F23,           Mask{Mod::NONE},                           "\033[23;2~",   AppKey::IGNORE,    0},
	{ KeyID::F24,           Mask{Mod::NONE},                           "\033[24;2~",   AppKey::IGNORE,    0},
	{ KeyID::F25,           Mask{Mod::NONE},                           "\033[1;5P",    AppKey::IGNORE,    0},
	{ KeyID::F26,           Mask{Mod::NONE},                           "\033[1;5Q",    AppKey::IGNORE,    0},
	{ KeyID::F27,           Mask{Mod::NONE},                           "\033[1;5R",    AppKey::IGNORE,    0},
	{ KeyID::F28,           Mask{Mod::NONE},                           "\033[1;5S",    AppKey::IGNORE,    0},
	{ KeyID::F29,           Mask{Mod::NONE},                           "\033[15;5~",   AppKey::IGNORE,    0},
	{ KeyID::F30,           Mask{Mod::NONE},                           "\033[17;5~",   AppKey::IGNORE,    0},
	{ KeyID::F31,           Mask{Mod::NONE},                           "\033[18;5~",   AppKey::IGNORE,    0},
	{ KeyID::F32,           Mask{Mod::NONE},                           "\033[19;5~",   AppKey::IGNORE,    0},
	{ KeyID::F33,           Mask{Mod::NONE},                           "\033[20;5~",   AppKey::IGNORE,    0},
	{ KeyID::F34,           Mask{Mod::NONE},                           "\033[21;5~",   AppKey::IGNORE,    0},
	{ KeyID::F35,           Mask{Mod::NONE},                           "\033[23;5~",   AppKey::IGNORE,    0},
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
