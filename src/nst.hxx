#ifndef NST_HXX
#define NST_HXX

namespace nst {

/// Nst main application class
class Nst {
public: // functions

	Nst();

	void run(int argc, const char **argv);

protected: // functions

	void mainLoop();

protected: // data
	cosmos::Init m_init;
};

} // end ns

#endif // inc. guard
