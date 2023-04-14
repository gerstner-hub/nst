#ifndef NST_HXX
#define NST_HXX

// cosmos
#include "cosmos/cosmos.hxx"

// X++
#include "X++/Xpp.hxx"

// nst
#include "Cmdline.hxx"
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
class Nst {
public: // functions

	Nst();

	/// this is the main entry point of the Nst application that is also
	/// passed the command line parameters for the program
	void run(int argc, const char **argv);

	TTY& tty() { return m_tty; }
	Term& term() { return m_term; }
	Selection& selection() { return m_selection; }
	WindowSystem& wsys() { return m_wsys; }
	const Cmdline& cmdline() const { return m_cmdline; }

	/// resize all necessary structures after the window or font size changed
	void resizeConsole();

protected: // functions

	void mainLoop();
	void waitForWindowMapping();
	/// sets up predefined environment variables for the terminal process
	void setEnv();

protected: // data

	cosmos::Init m_init;
	xpp::Init m_xpp;
	Cmdline m_cmdline;
	WindowSystem m_wsys;
	Term m_term;
	TTY m_tty;
	Selection m_selection;
	XEventHandler m_event_handler;
};

} // end ns

#endif // inc. guard
