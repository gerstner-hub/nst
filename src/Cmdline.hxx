#pragma once

// stdlib
#include <string>

// TCLAP
#include "tclap/CmdLine.h"

namespace nst {

/// nst command line parameter handling and storage.
class Cmdline :
		public TCLAP::CmdLine {
public: // functions

	Cmdline();

	/// returns the string that should be used as terminal window title
	const std::string& title() const {
		auto &rst = rest.getValue();

		if (!window_title.isSet() && !tty_line.isSet() && !rst.empty()) {
			// use command basename as title
			return rst[0];
		}

		// use supplied or default value
		return window_title.getValue();
	}

	/// returns whether XLib should be set to XSync() mode
	bool useXSync() const;

public: // data

	TCLAP::SwitchArg use_alt_screen;
	TCLAP::SwitchArg fixed_geometry;
	TCLAP::SwitchArg execute;
	TCLAP::SwitchArg list_themes;
	TCLAP::ValueArg<std::string> window_class;
	TCLAP::ValueArg<std::string> window_name;
	TCLAP::ValueArg<std::string> window_title;
	TCLAP::ValueArg<std::string> window_geometry;
	TCLAP::ValueArg<std::string> font;
	TCLAP::ValueArg<std::string> iofile;
	TCLAP::ValueArg<std::string> cwd;
	TCLAP::ValueArg<unsigned long> embed_window;
	TCLAP::ValueArg<std::string> tty_line;
	TCLAP::ValueArg<std::string> config_file;
	TCLAP::ValueArg<std::string> theme;
	TCLAP::UnlabeledMultiArg<std::string> rest;
};

} // end ns
