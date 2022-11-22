#ifndef NST_HXX
#define NST_HXX

// cosmos
#include "cosmos/Init.hxx"

// nst
#include "TTY.hxx"

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

protected: // functions

	void mainLoop();
	void waitForWindowMapping();

protected: // data
	cosmos::Init m_init;
	TermWindow &m_term_win;
	X11 &m_x11;
	TTY m_tty;
	static Nst *the_instance;
};

} // end ns

#endif // inc. guard
