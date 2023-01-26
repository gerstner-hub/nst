#ifndef NST_TTY_HXX
#define NST_TTY_HXX

// C++
#include <string_view>

// cosmos
#include "cosmos/fs/StreamFile.hxx"
#include "cosmos/io/Poller.hxx"
#include "cosmos/io/Terminal.hxx"
#include "cosmos/proc/SubProc.hxx"
#include "cosmos/types.hxx"

namespace nst {

class Nst;

/// Pseudo Terminal I/O
/**
 * This class covers the PTY/TTY interfacing towards the operating system.
 * It's job is mainly the raw I/O handling and handling of low level TTY
 * aspects.
 *
 * It holds the sub process that is running in the terminal. It sends
 * keyboard input and out-of-band data to the child process and receives data
 * from it to display on the terminal.
 **/
class TTY {
public: // types

	using MayEcho = cosmos::NamedBool<struct echo_t, true>;

public: // functions

	explicit TTY(Nst &nst) : m_nst(nst) {}

	~TTY();

	/// opens the proper TTY device and returns a file descriptor for it
	/**
	 * The file descriptor is only returned for monitoring purposes, the
	 * ownership still belongs to TTY and I/O on it should only be
	 * performed by this class.
	 **/
	cosmos::FileDescriptor create();

	/// reads data from the TTY and forwards it to the active Term instance
	/**
	 * \return The number of bytes that have been read, 0 on EOF or other
	 * I/O error conditions.
	 **/
	size_t read();

	/// provide input to the child process e.g. character input from key presses
	/**
	 * \param[in] echo If set then the input will also be forwarded to the
	 * Term class to display on the window.
	 **/
	void write(const std::string_view &sv, const MayEcho echo);

	/// inform the TTY device (and thus the child process) about a size change
	void resize(const Extent &size);

	/// sends SIGHUP to the child process, informing it that we're quitting
	void hangup();

	/// prints the given data into the raw I/O file, if configured
	void printToIoFile(const std::string_view &s) {
		if (!m_io_file.isOpen())
			return;
		doPrintToIoFile(s);
	}

	/// returns a FileDescriptor object for the pidfd representing the
	/// child process running in the terminal
	auto getChildFD() { return m_child_proc.pidFD(); }

	/// to be called when a SIGCHLD was received in the main loop
	/**
	 * This will throw an exception if the child process did not exit
	 * cleanly
	 **/
	void handleSigChildEvent();

	/// sends a stream of zero bits to the peer for a given duration
	void sendBreak();

protected: // functions

	/// open a real TTY
	void openTTY(const std::string &line);
	/// runs stty to configure a real TTY device if specified on the cmdline
	void configureTTY();
	/// create a PTY to operate on
	void createPTY();
	/// setup m_cmd_poller to listen on m_cmd_file
	void setupPoller();
	/// opens a I/O file where all TTY I/O is printed to, raw
	void setupIOFile(const std::string &path);
	/// forward data unmodified to the child process
	void writeRaw(const std::string_view &sv);
	/// for the PTY case execute the default shell or the program passed on the command line
	void executeShell(cosmos::FileDescriptor slave);
	void doPrintToIoFile(const std::string_view &s);

protected: // data

	Nst &m_nst;
	cosmos::SubProc m_child_proc; /// the actual child process running in the terminal
	cosmos::StreamFile m_io_file; /// I/O file which receives all data displayed on the terminal
	cosmos::StreamFile m_cmd_file; /// master end of pty or real TTY device
	cosmos::Poller m_cmd_poller; /// event driven I/O for m_cmd_file
	cosmos::Terminal m_terminal; /// wrapper around m_cmd_file for TTY ioctls
	char m_buf[BUFSIZ]; /// holds data read from the TTY not yet forwarded to Term
	size_t m_buf_bytes = 0; /// number of unprocessed bytes in m_buf
};

} // end ns

#endif // inc. guard
