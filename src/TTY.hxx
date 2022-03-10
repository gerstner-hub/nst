#ifndef NST_TTY_HXX
#define NST_TTY_HXX

// nst
#include "Term.hxx"

// cosmos
#include "cosmos/types.hxx"
#include "cosmos/fs/StreamFile.hxx"
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

protected: // data

	Term *m_term;
	pid_t m_pid = -1;
	int m_cmdfd = -1;
	cosmos::SignalFD m_child_sig_fd;
	cosmos::StreamFile m_io_file;

public: // functions

	TTY() { m_term = &term; }
	int create(const Params &pars);
	size_t read();
	void write(const char *s, size_t n, bool may_echo);
	void resize(int tw, int th);
	void hangup();
	void printToIoFile(const char *s, size_t len) {
		if (!m_io_file.isOpen())
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
	void sendBreak();
};

} // end ns

extern nst::TTY g_tty;

#endif // inc. guard
