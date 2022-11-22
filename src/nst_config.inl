#include <functional>

#include "Selection.hxx"

namespace nst {

/* these are placed in this separate file since they are included from the
 * x.cxx compilation unit exclusively as they require function definitions
 * from there.
 */

/* function definitions used from x.cxx */
static void clipcopy();
static void clippaste();
static void numlock();
static void selpaste();
static void zoom(float);
static void zoomabs(float);
static void zoomreset();
static void ttysend(const char *);
static void printscreen();
static void printsel();
static void toggleprinter();
// from TTY
void sendbreak();

namespace config {

/*
 * Internal mouse shortcuts.
 * Beware that overloading Button1 will disable the selection.
 */
const std::array<MouseShortcut, 5> MSHORTCUTS({
	             /* mask                 button   function                         release */
	MouseShortcut{ XK_ANY_MOD,           Button2, selpaste,                        true },
	MouseShortcut{ ShiftMask,            Button4, std::bind(ttysend, "\033[5;2~"), false },
	MouseShortcut{ XK_ANY_MOD,           Button4, std::bind(ttysend, "\031"),      false },
	MouseShortcut{ ShiftMask,            Button5, std::bind(ttysend, "\033[6;2~"), false },
	MouseShortcut{ XK_ANY_MOD,           Button5, std::bind(ttysend, "\005"),      false },
});

/* Internal keyboard shortcuts. */
#define MODKEY Mod1Mask
#define TERMMOD (ControlMask|ShiftMask)

const Shortcut SHORTCUTS[] = {
	/* mask                 keysym          function */
	{ XK_ANY_MOD,           XK_Break,       sendbreak           },
	{ ControlMask,          XK_Print,       toggleprinter       },
	{ ShiftMask,            XK_Print,       printscreen         },
	{ XK_ANY_MOD,           XK_Print,       printsel            },
	{ TERMMOD,              XK_Prior,       std::bind(zoom, +1) },
	{ TERMMOD,              XK_Next,        std::bind(zoom, -1) },
	{ TERMMOD,              XK_Home,        zoomreset           },
	{ TERMMOD,              XK_C,           clipcopy            },
	{ TERMMOD,              XK_V,           clippaste           },
	{ TERMMOD,              XK_Y,           selpaste            },
	{ ShiftMask,            XK_Insert,      selpaste            },
	{ TERMMOD,              XK_Num_Lock,    numlock             },
};

/*
 * Selection types' masks.
 * Use the same masks as usual.
 * Button1Mask is always unset, to make masks match between ButtonPress.
 * ButtonRelease and MotionNotify.
 * If no match is found, regular selection is used.
 */
constexpr std::array<std::pair<Selection::Type, unsigned>, 2> SELMASKS = {
	std::pair{Selection::Type::REGULAR,     0},
	         {Selection::Type::RECTANGULAR, Mod1Mask},
};

} // ns config
} // ns nst
