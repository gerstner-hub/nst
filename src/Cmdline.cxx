// cosmos
#include "cosmos/proc/process.hxx"

// nst
#include "Cmdline.hxx"
#include "nst_config.hxx"

namespace nst {

Cmdline::Cmdline() :
		TCLAP::CmdLine{"not (so) simple terminal emulator", ' ', VERSION},
	use_alt_screen{ "a", "no-alt-screen",  "disable the alternative screen buffer", *this, true},
	fixed_geometry{ "i", "fixed-geometry", "fixate the position specified via -g", *this, false},
	execute{        "e", "",               "execute remaining parameters as command. Only for backward compatibility.", *this, false},
	window_class{   "c", "window-class",   "defines the window class (default $TERM)", false, config::TERM_NAME.data(), "string", *this},
	window_name{    "n", "window-name",    "defines the window instance name (default $TERM)", false, config::TERM_NAME.data(), "string", *this},
	// NOTE: original st also allowed -T title but TCLAP doesn't seem to
	// support multiple short chars for the same switch
	window_title{   "t", "window-title",   "defines the window title (default 'nst')", false, "nst", "string", *this},
	window_geometry{"g", "geometry",       "defines the window geometry. e.g. 100x40+100+100.", false, "", "X11 gemeoetry", *this},
	font{           "f", "font",           "defines the font to use when nst is run.", false, nst::config::FONT.data(), "fontconfig font name", *this},
	iofile{         "o", "iofile",         "writes all the I/O to the given file for recording. '-' means stdout.", false, "", "path", *this},
	embed_window{   "w", "embed-window",   "embeds nst within the window identified by given windowid", false, 0, "window-id", *this},
	tty_line{       "l", "tty",            "use a tty line instead of pseudo terminal. Remaining parameters will be passed as flags to stty", false, "", "path-to-tty", *this},
	rest{                "rest",           "command to execute instead of shell for -e, or if -l is not given. If -l is given then these are stty parameters", false, "extra-pars", *this}
{}

bool Cmdline::useXSync() const {
	// this is not really command line parsing, but we could add a switch
	// to the command line in the future.
	// this way all runtime configuration settings are kept in this class.
	return cosmos::proc::exists_env_var("NST_XSYNC");
}

} // end ns
