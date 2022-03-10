#ifndef NST_TTY_HXX
#define NST_TTY_HXX

// nst
#include "Term.hxx"

// cosmos
#include "cosmos/types.hxx"
#include "cosmos/proc/SignalFD.hxx"

namespace nst {

void sendbreak(const Arg *);

class TTY {
	friend void sendbreak(const Arg*);
public: // types

	struct Params {
		std::string line;
		std::string cmd;
		std::string out;
		const cosmos::StringVector &args;
	};

public: // data
	int iofd = -1;

protected: // data

	Term *m_term;
	pid_t m_pid = -1;
	int m_cmdfd = -1;
	cosmos::SignalFD m_child_sig_fd;

public: // functions

	TTY() { m_term = &term; }
	int create(const Params &pars);
	size_t read();
	void write(const char *s, size_t n, bool may_echo);
	void resize(int tw, int th);
	void hangup();
	void sendBreak();
	void printToIoFile(const char *s, size_t len) {
		if (iofd == -1)
			return;
		doPrintToIoFile(s, len);
	}

	auto getSigFD() { return m_child_sig_fd.raw().raw(); }
	void sigChildEvent();

protected: // functions

	void writeRaw(const char *s, size_t n);
	void runStty(const Params &pars);
	void executeShell(const Params &pars);
	void doPrintToIoFile(const char *s, size_t len);
	void initSignalHandling();
};

} // end ns

extern nst::TTY g_tty;

#endif // inc. guard
