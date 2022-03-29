#ifndef NST_CMDLINE_HXX
#define NST_CMDLINE_HXX

// stdlib
#include <string>

// TCLAP
#include <tclap/CmdLine.h>

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

} // end ns

#endif // inc. guard
