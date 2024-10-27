#pragma once

// cosmos
#include "cosmos/io/StdLogger.hxx"
#include "cosmos/main.hxx"
#include "cosmos/string.hxx"

// xpp
#include "xpp/Xpp.hxx"

// nst
#include "Cmdline.hxx"
#include "ConfigFile.hxx"
#include "Selection.hxx"
#include "Term.hxx"
#include "TTY.hxx"
#include "XEventHandler.hxx"
#include "WindowSystem.hxx"

namespace nst {

/// Nst main application class.
/**
 * This type holds instances of all the other types that make up nst. It
 * implements the main loop and is invoked from the main function of the
 * program.
 **/
class Nst :
		public cosmos::MainPlainArgs {
public: // functions

	Nst();

	TTY& tty() { return m_tty; }
	Term& term() { return m_term; }
	Selection& selection() { return m_selection; }
	WindowSystem& wsys() { return m_wsys; }
	const Cmdline& cmdline() const { return m_cmdline; }

	/// resize all necessary structures after the window or font size changed
	void resizeConsole();

	/// Pipe the current terminal contents (including scrollback) to the given program.
	/**
	 * The data will be provided to the program's stdin. The call will run
	 * synchronously i.e. the terminal won't continue running until the
	 * child process has read all data and exits.
	 *
	 * This is intended for graphical programs like gvim which daemonize
	 * and continue running the background.
	 *
	 * \param[in] cmdline The path to the program to run as well any
	 * additional command line parameters.
	 **/
	void pipeBufferToExternalCommand();

	/// Access to the central logger instance.
	auto& logger() const {
		return m_logger;
	}

	/// Access to configuration file data.
	auto& configFile() const {
		return m_config_file;
	}

	const auto& theme() const {
		return m_theme;
	}

	bool setTheme(const std::string_view name);

protected: // functions

	/// this is the main entry point of the Nst application that is also
	/// passed the command line parameters for the program
	cosmos::ExitStatus main(int argc, const char **argv) override;

	void mainLoop();
	void setupSignals();
	void waitForWindowMapping();
	/// sets up predefined environment variables for the terminal process
	void setEnv();
	void loadConfig();
	void applyThemeFromCmdline(const std::string_view theme_name);
	void applyCWDFromCmdline(const std::string& cwd);

protected: // data

	xpp::Init m_xpp;
	mutable cosmos::StdLogger m_logger;
	Theme m_theme;
	ConfigFile m_config_file;
	cosmos::StringVector m_pipe_buffer_command;
	Cmdline m_cmdline;
	WindowSystem m_wsys;
	Term m_term;
	TTY m_tty;
	Selection m_selection;
	XEventHandler m_event_handler;
	std::chrono::milliseconds m_blink_timeout;
};

} // end ns
