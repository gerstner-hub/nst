#ifndef NST_HXX
#define NST_HXX

// cosmos
#include "cosmos/Init.hxx"

// nst
#include "Cmdline.hxx"
#include "Selection.hxx"
#include "Term.hxx"
#include "TTY.hxx"
#include "XEventHandler.hxx"
#include "x.hxx"

namespace nst {

/// Nst main application class
class Nst {
public: // functions

	Nst();

	void run(int argc, const char **argv);

	// accessors to global-like objects until refactoring reaches a state
	// where this becomes unnecessary
	TTY& getTTY() { return m_tty; }
	Term& getTerm() { return m_term; }
	Selection& getSelection() { return m_selection; }
	X11& getX11() { return m_x11; }
	auto& getCmdline() const { return m_cmdline; }
	void resizeConsole(const Extent &win = {0,0});

protected: // functions

	void mainLoop();
	void waitForWindowMapping();
	void applyCmdline();
	/// sets up environment variables for the terminal process
	void setEnv();

protected: // data

	cosmos::Init m_init;
	Cmdline m_cmdline;
	X11 m_x11;
	Term m_term;
	TTY m_tty;
	Selection m_selection;
	XEventHandler m_event_handler;
};

} // end ns

#endif // inc. guard
