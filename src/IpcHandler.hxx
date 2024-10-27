#pragma once

// C++
#include <string>
#include <deque>
#include <optional>

// cosmos
#include "cosmos/io/Poller.hxx"
#include "cosmos/net/UnixConnection.hxx"
#include "cosmos/net/UnixListenSocket.hxx"

// nst
#include "fwd.hxx"

namespace nst {

/// UNIX domain socket IPC handler.
/**
 * This class deals with IPC socket requests.
 *
 * nst is listening on an abstract UNIX domain socket path for client
 * connections to access its interface. Currently the IPC is mainly used for
 * accessing the terminal screen and history contents.
 *
 * Only one session is allowed in parallel. Either the listener socket is
 * monitored in the Poller for new connection requests, or an active
 * connection is monitored for I/O.
 *
 * Clients send a request in form of an IpcHandler::Message value. The
 * IpcHandler processes requests and replies with data, if applicable.
 **/
class IpcHandler {
public: // functions

	/// Create an IpcHandler.
	/**
	 * \param[in,out] poller The Poller object used in the nst main loop.
	 * IpcHandler needs to adjust which sockets are monitored for which
	 * I/O events.
	 **/
	explicit IpcHandler(Nst &nst, cosmos::Poller &poller) :
			m_nst{nst},
			m_poller{poller} {}

	/// Returns the address used for m_listener.
	static std::string address();

	/// Create the IPC endpoint and start accepting connections.
	void init();

	/// Inspect the given poller event and act on I/O if necessary.
	/**
	 * \return indicator whether the screen should be redrawn due to the
	 * changes introduced by the event.
	 **/
	bool checkEvent(const cosmos::Poller::PollEvent &event);

public: // types

	/// Different IPC message types. This is what a client request needs to send in its initial message.
	enum class Message : uint16_t {
		INVALID = UINT16_MAX,
		/// Store a snapshot of the current terminal history to operate on.
		SNAPSHOT_HISTORY = 1,
		/// Get the current terminal buffer (including history) content.
		GET_HISTORY,
		/// Get the complete terminal buffer stored in the last snapshot.
		GET_SNAPSHOT,
		/// Test message that triggers an identical reply.
		PING,
		/// Send the current working directory of the terminal's child process.
		GET_CWD,
		/// Change the active theme.
		SET_THEME
	};

public: // data

	/// Largest packet size to send/receive.
	static constexpr size_t MAX_CHUNK_SIZE = 1024 * 64;

protected: // types

	/// The current state of an IPC session.
	enum class State {
		/// Waiting for new connections.
		WAITING,
		/// A connection is being processed, request is being collected.
		RECEIVING,
		/// Ongoing transmission to fulfill a request.
		SENDING
	};

protected: // functions

	/// Handles the initial I/O on an IPC connection.
	bool receiveCommand();

	/// Receive arbitrary data from the current connection.
	/**
	 * This function can return a size larger than \c max_size when the
	 * received message has been truncated.
	 *
	 * On error an exception is thrown, the session will be closed in this
	 * case.
	 **/
	size_t receiveData(char *buffer, const size_t max_size);

	/// Once a valid request has been received this processes it.
	bool processCommand(const Message message);

	/// Stores the given RPC result in m_send_queue.
	void queueStatus(const cosmos::ExitStatus status);

	/// Handles a SET_THEME command.
	bool handleSetTheme();

	/// If we need to reply with data then this manages the transmission.
	void sendData();

	/// Closes all session state and accepts new connections again.
	void closeSession();

	/// Returns the current history buffer.
	std::string history() const;

	/// Accept a new connection, checking the peer's permissions.
	void acceptConnection();

	std::string childCWD() const;

protected: // data

	Nst &m_nst;
	cosmos::Poller &m_poller;
	State m_state = State::WAITING;
	cosmos::UnixSeqPacketListenSocket m_listener;
	std::optional<cosmos::UnixConnection> m_connection;
	std::string m_snapshot;
	cosmos::ExitStatus m_send_status = cosmos::ExitStatus::SUCCESS;
	std::deque<std::string> m_send_queue;
	size_t m_msg_pos = 0; ///< number of bytes of front element in m_send_queue that have already been sent
};

} // end ns
