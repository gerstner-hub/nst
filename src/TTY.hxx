#ifndef NST_TTY_HXX
#define NST_TTY_HXX

// nst
#include "Term.hxx"

// cosmos
#include "cosmos/fs/StreamFile.hxx"
#include "cosmos/io/Poller.hxx"
#include "cosmos/io/Terminal.hxx"
#include "cosmos/proc/SignalFD.hxx"
#include "cosmos/proc/SubProc.hxx"
#include "cosmos/types.hxx"

namespace nst {

class Term;
class Cmdline;

class TTY {
	friend void sendbreak();

protected: // data

	Term &m_term;
	cosmos::SubProc m_child_proc;
	cosmos::StreamFile m_io_file;
	/// master end of pty
	cosmos::StreamFile m_cmd_file;
	cosmos::Poller m_cmd_poller;
	cosmos::Terminal m_pty;
	char m_buf[BUFSIZ];
	size_t m_buf_bytes = 0;

public: // functions

	TTY(Term *term) : m_term(*term) {}
	~TTY();
	cosmos::FileDescriptor create(const Cmdline &cmdline);
	size_t read();
	void write(const char *s, size_t n, bool may_echo);
	void resize(const Extent &size);
	void hangup();
	void printToIoFile(const char *s, size_t len) {
		if (!m_io_file.isOpen())
			return;
		doPrintToIoFile(s, len);
	}

	auto getChildFD() { return m_child_proc.pidFD(); }
	void sigChildEvent();

protected: // functions

	void setupIOFile(const std::string &path);
	void writeRaw(const char *s, size_t n);
	void runStty(const Cmdline &cmdline);
	void executeShell(const Cmdline &cmdline, cosmos::FileDescriptor slave);
	void doPrintToIoFile(const char *s, size_t len);
	void sendBreak();
};

} // end ns

#endif // inc. guard
