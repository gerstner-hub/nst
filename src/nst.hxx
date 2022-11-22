#ifndef NST_HXX
#define NST_HXX

namespace nst {

struct TermWindow;
struct X11;

/// Nst main application class
class Nst {
public: // functions

	Nst();

	void run(int argc, const char **argv);

protected: // functions

	void mainLoop();
	void waitForWindowMapping();

protected: // data
	cosmos::Init m_init;
	TermWindow &m_term_win;
	X11 &m_x11;
};

} // end ns

#endif // inc. guard
