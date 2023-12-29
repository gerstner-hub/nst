// C++
#include <iostream>

// TCLAP
#include "tclap/CmdLine.h"

// cosmos
#include "cosmos/cosmos.hxx"
#include "cosmos/error/ApiError.hxx"
#include "cosmos/net/UnixClientSocket.hxx"
#include "cosmos/proc/process.hxx"

// nst
#include "IpcHandler.hxx"

namespace nst {

/// Command line parser for the IpcClient tool.
class Cmdline :
		public TCLAP::CmdLine {
public: // functions

	Cmdline();

public: // data

	TCLAP::SwitchArg save_snapshot;
	TCLAP::SwitchArg get_snapshot;
	TCLAP::SwitchArg get_history;
	TCLAP::SwitchArg test_connection;

protected: // data

	TCLAP::OneOf m_xor_group;
};

Cmdline::Cmdline() :
		TCLAP::CmdLine{"nst terminal emulator IPC utility", ' ', VERSION},
		save_snapshot{"S", "snapshot", "save a snapshot of the current history"},
		get_snapshot{"s", "get-snapshot", "print the history data from the last snapshot to stdout"},
		get_history{"d", "get-history", "print (dump) the current history data to stdout"},
		test_connection{"t", "test", "only test the connection to the nst terminal, returns zero on success, non-zero otherwise"} {
	m_xor_group.add(save_snapshot);
	m_xor_group.add(get_snapshot);
	m_xor_group.add(get_history);
	m_xor_group.add(test_connection);
	this->add(m_xor_group);
}

/// IPC client utility for nst.
/**
 * This utility allows to connect to the currently running nst terminal and
 * to access its IPC features.
 **/
class IpcClient {
public: // functions

	void run(int argc, const char **argv);

protected: // types

	using Message = IpcHandler::Message;

protected: // functions

	cosmos::UnixConnection connect();

	/// Receives data after a request has been dispatched.
	void receiveData(const Message request, cosmos::UnixConnection &connection);

protected: // data

	cosmos::Init m_init;
	cosmos::UnixSeqPacketClientSocket m_sock;
	Cmdline m_cmdline;

	static constexpr cosmos::ExitStatus CONN_ERR{2};
	static constexpr cosmos::ExitStatus INT_ERR{5};
};

void IpcClient::run(int argc, const char **argv) {
	m_cmdline.parse(argc, argv);

	const auto request = [this]() -> Message {
		if (m_cmdline.save_snapshot.isSet())
			return Message::SNAPSHOT_HISTORY;
		else if (m_cmdline.get_snapshot.isSet())
			return Message::GET_SNAPSHOT;
		else if (m_cmdline.get_history.isSet())
			return Message::GET_HISTORY;
		else if (m_cmdline.test_connection.isSet())
			return Message::PING;
		else {
			throw INT_ERR;
		}
	}();

	auto connection = connect();
	connection.send(&request, sizeof(request));
	receiveData(request, connection);
}

cosmos::UnixConnection IpcClient::connect() {
	constexpr auto envvar = "NST_IPC_ADDR";
	const auto ipc_addr = cosmos::proc::get_env_var(envvar);

	if (!ipc_addr) {
		if (!m_cmdline.test_connection.isSet()) {
			std::cerr << "Environment variable '" << envvar << "' is not set. Cannot connect to nst.\n";
		}

		throw CONN_ERR;
	}

	try {
		return m_sock.connect(cosmos::UnixAddress{*ipc_addr, cosmos::UnixAddress::AbstractAddress{true}});
	} catch (const cosmos::ApiError &error) {
		if (!m_cmdline.test_connection.isSet()) {
			std::cerr << "Failed to connect to nst (address: @" << *ipc_addr << "): " << error.what() << "\n";
		}

		throw CONN_ERR;
	}
}

void IpcClient::receiveData(const Message request, cosmos::UnixConnection &connection) {
	std::string buffer;
	while (true) {
		buffer.resize(IpcHandler::MAX_CHUNK_SIZE);
		auto len = connection.receive(buffer.data(), buffer.size(), cosmos::MessageFlags{cosmos::MessageFlag::TRUNCATE});
		if (len == 0) {
			if (m_cmdline.test_connection.isSet()) {
				// should have received a PING reply
				throw INT_ERR;
			}
			return;
		} else if(len > buffer.size()) {
			std::cerr << "IPC packet was truncated from " << len << " to " << buffer.size() << "!\n";
			len = buffer.size();
		}

		buffer.resize(len);

		if (request == Message::SNAPSHOT_HISTORY) {
			std::cerr << "received unexpected data as reply to SNAPSHOT_HISTORY\n";
			throw INT_ERR;
		} else if (request == Message::PING) {
			Message reply;
			if (buffer.size() != sizeof(reply)) {
				std::cerr << "received bad reply len for PING message\n";
				throw INT_ERR;
			}

			std::memcpy(&reply, buffer.data(), sizeof(reply));
			if (reply != Message::PING) {
				std::cerr << "received bad PING reply message\n";
				throw INT_ERR;
			}

			// PING test succeeded
			return;
		} else {
			// simply forward data to stdout
			std::cout << buffer;
		}
	}
}

} // end ns

int main(int argc, const char **argv) {
	try {
		nst::IpcClient ipc_client;
		ipc_client.run(argc, argv);
		return EXIT_SUCCESS;
	} catch (const cosmos::ExitStatus status) {
		return cosmos::to_integral(status);
	} catch (const std::exception &ex) {
		std::cerr << ex.what() << std::endl;
		return EXIT_FAILURE;
	}
}
