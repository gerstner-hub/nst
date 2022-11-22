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

	const std::string& getTitle() const {
		auto &rst = rest.getValue();

		if (!window_title.isSet() && !tty_line.isSet() && !rst.empty()) {
			// use command basename as title
			return rst[0];
		}

		// use supplied or default value
		return window_title.getValue();
	}

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
	TCLAP::ValueArg<unsigned long> embed_window;
	TCLAP::ValueArg<std::string> tty_line;
	TCLAP::UnlabeledMultiArg<std::string> rest;
};

} // end ns

#endif // inc. guard
