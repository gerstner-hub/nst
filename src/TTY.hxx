#ifndef NST_TTY_HXX
#define NST_TTY_HXX

// nst
#include "Term.hxx"

// cosmos
#include "cosmos/types.hxx"

class TTY {
	friend void sigchld(int);
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

	Term *m_term = nullptr;
	pid_t m_pid = -1;
	int m_cmdfd = -1;

public: // functions

	void setTerm(Term &term) { m_term = &term; }
	int create(const Params &pars);
	size_t read();
	void write(const char *s, size_t n, bool may_echo);
	void resize(int tw, int th);
	void hangup();
	void sendBreak();

protected: // functions

	void writeRaw(const char *s, size_t n);
	void runStty(const Params &pars);
	void executeShell(const Params &pars);
	void sigChildEvent();
};

extern TTY g_tty;

#endif // inc. guard
