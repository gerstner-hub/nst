#pragma once

// C++
#include <string>
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
	 * \param[in-out] poller The Poller object used in the nst main loop.
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
	void checkEvent(const cosmos::Poller::PollEvent &event);

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

	/// Handles I/O on an IPC connection.
	void receiveCommand();

	/// Once a valid request has been received this processes it.
	void processCommand(const Message message);

	/// If we need to reply with data then this manages the transmission.
	void sendData();

	/// Closes all session state and accepts new connections again.
	void closeSession();

	/// Returns the current history buffer.
	std::string history() const;

	/// Accept a new connection, checking the peer's permissions.
	void acceptConnection();

protected: // data

	Nst &m_nst;
	cosmos::Poller &m_poller;
	State m_state = State::WAITING;
	cosmos::UnixSeqPacketListenSocket m_listener;
	std::optional<cosmos::UnixConnection> m_connection;
	std::string m_snapshot;
	std::string m_send_data;
	size_t m_send_pos = 0;
};

} // end ns
