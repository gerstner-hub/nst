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

namespace nst {

struct TermWindow;
struct X11;

/// Nst main application class
class Nst {
public: // functions

	Nst();

	void run(int argc, const char **argv);

	// accessors to global-like objects until refactoring reaches a state
	// where this becomes unnecessary
	static TTY& getTTY() { return the_instance->m_tty; }
	static Term& getTerm() { return the_instance->m_term; }
	static Selection& getSelection() { return the_instance->m_selection; }
	static Nst& getInstance() { return *the_instance; }
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
	TermWindow &m_term_win;
	X11 &m_x11;
	TTY m_tty;
	Term m_term;
	Selection m_selection;
	XEventHandler m_event_handler;
	static Nst *the_instance;
};

} // end ns

#endif // inc. guard
