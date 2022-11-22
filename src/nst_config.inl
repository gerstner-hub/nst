namespace nst {

/* these are placed in this separate file since they are included from the
 * x.cxx compilation unit exclusively as they require function definitions
 * from there.
 */

/* function definitions used from x.cxx */
static void clipcopy(const Arg *);
static void clippaste(const Arg *);
static void numlock(const Arg *);
static void selpaste(const Arg *);
static void zoom(const Arg *);
static void zoomabs(const Arg *);
static void zoomreset(const Arg *);
static void ttysend(const Arg *);
static void printscreen(const Arg *);
static void printsel(const Arg *);
static void toggleprinter(const Arg *);
// from TTY
void sendbreak(const Arg *);

namespace config {

/*
 * Internal mouse shortcuts.
 * Beware that overloading Button1 will disable the selection.
 */
constexpr std::array<MouseShortcut, 5> MSHORTCUTS({
	             /* mask                 button   function        argument       release */
	MouseShortcut{ XK_ANY_MOD,           Button2, selpaste,       {.i = 0},      true },
	MouseShortcut{ ShiftMask,            Button4, ttysend,        {.s = "\033[5;2~"}, false },
	MouseShortcut{ XK_ANY_MOD,           Button4, ttysend,        {.s = "\031"}, false },
	MouseShortcut{ ShiftMask,            Button5, ttysend,        {.s = "\033[6;2~"}, false },
	MouseShortcut{ XK_ANY_MOD,           Button5, ttysend,        {.s = "\005"}, false },
});

/* Internal keyboard shortcuts. */
#define MODKEY Mod1Mask
#define TERMMOD (ControlMask|ShiftMask)

const Shortcut SHORTCUTS[] = {
	/* mask                 keysym          function        argument */
	{ XK_ANY_MOD,           XK_Break,       sendbreak, {.i =  0} },
	{ ControlMask,          XK_Print,       toggleprinter,  {.i =  0} },
	{ ShiftMask,            XK_Print,       printscreen,    {.i =  0} },
	{ XK_ANY_MOD,           XK_Print,       printsel,       {.i =  0} },
	{ TERMMOD,              XK_Prior,       zoom,           {.f = +1} },
	{ TERMMOD,              XK_Next,        zoom,           {.f = -1} },
	{ TERMMOD,              XK_Home,        zoomreset,      {.f =  0} },
	{ TERMMOD,              XK_C,           clipcopy,       {.i =  0} },
	{ TERMMOD,              XK_V,           clippaste,      {.i =  0} },
	{ TERMMOD,              XK_Y,           selpaste,       {.i =  0} },
	{ ShiftMask,            XK_Insert,      selpaste,       {.i =  0} },
	{ TERMMOD,              XK_Num_Lock,    numlock,        {.i =  0} },
};

} // ns config
} // ns nst
