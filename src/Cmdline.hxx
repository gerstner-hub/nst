#ifndef NST_CMDLINE_HXX
#define NST_CMDLINE_HXX

// stdlib
#include <string>

// TCLAP
#include <tclap/CmdLine.h>

// nst
#include "nst_config.h"

namespace nst {

/// nst command line parameter handling
class Cmdline :
	public TCLAP::CmdLine {
public: // functions

	Cmdline();
public: // data
	TCLAP::SwitchArg use_alt_screen;
	TCLAP::SwitchArg fixed_geometry;
	TCLAP::SwitchArg execute;
	TCLAP::ValueArg<std::string> window_class;
	TCLAP::ValueArg<std::string> window_name;
	TCLAP::ValueArg<std::string> window_title;
	TCLAP::ValueArg<std::string> window_geometry;
	TCLAP::ValueArg<std::string> font;
	TCLAP::ValueArg<std::string> iofile;
	TCLAP::ValueArg<std::string> embed_window;
	TCLAP::ValueArg<std::string> tty_line;
	TCLAP::UnlabeledMultiArg<std::string> rest;
};

Cmdline::Cmdline() :
	TCLAP::CmdLine("not (so) simple terminal emulator", ' ', VERSION),
	use_alt_screen("a", "no-alt-screen", "disable the alternative screen buffer", *this, true),
	fixed_geometry("i", "fixed-geometry", "fixate the position specified via -g", *this, false),
	execute("e", "", "execute remaining parameters as commeand. Only for backward compatibility.", *this, false),
	window_class("c", "window-class", "defines the window class (default $TERM)", false, "", "string", *this),
	window_name("n", "window-name", "defines the window instance name (default $TERM)", false, "", "string", *this),
	// NOTE: original st also allowed -T title but TCLAP doesn't seem to
	// support multiple short chars for the same switch
	window_title("t", "window-title", "defines the window title (default 'nst')", false, "nst", "string", *this),
	window_geometry("g", "geometry", "defines the window geometry. e.g. 100x40+100+100.", false, "", "X11 gemeoetry", *this),
	font("f", "font", "defines the font to use when st is run.", false, nst::config::FONT,
		"fontconfig font name", *this),
	iofile("o", "iofile", "writes all the I/O to the given file for recording. '-' means stdout.", false, "", "path", *this),
	embed_window("w", "embed-window", "embeds nst within the window identified by given windowid", false, "", "window-id", *this),
	tty_line("l", "tty", "use a tty line instead of pseudo terminal. Remaining parameters will be passed as flags to stty", false, "", "path-to-tty", *this),
	rest("rest", "command to execute instead of shell for -e, or if -l is not given. If -l is given then these are stty parameters", false, "extra-pars", *this)
{
}

} // end ns

#endif // inc. guard
