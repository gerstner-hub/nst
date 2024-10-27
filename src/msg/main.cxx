// C++
#include <iostream>
#include <fstream>
#include <set>
#include <sstream>

// TCLAP
#include "tclap/CmdLine.h"

// cosmos
#include "cosmos/cosmos.hxx"
#include "cosmos/error/ApiError.hxx"
#include "cosmos/error/FileError.hxx"
#include "cosmos/main.hxx"
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
	TCLAP::SwitchArg get_global_history;
	TCLAP::SwitchArg test_connection;
	TCLAP::SwitchArg get_cwds;
	TCLAP::ValueArg<std::string> set_theme;
	TCLAP::ValueArg<std::string> instance;

protected: // data

	TCLAP::OneOf m_xor_group;
};

Cmdline::Cmdline() :
		TCLAP::CmdLine    {"nst terminal emulator IPC utility", ' ', NST_VERSION},
		save_snapshot     {"S", "snapshot", "save a snapshot of the current history"},
		get_snapshot      {"s", "get-snapshot", "print the history data from the last snapshot to stdout"},
		get_history       {"d", "get-history", "print (dump) the current history data to stdout"},
		get_global_history{"D", "get-global-history", "print (dump) the current history of all available NST terminals to stdout"},
		test_connection   {"t", "test", "only test the connection to the nst terminal, returns zero on success, non-zero otherwise"},
		get_cwds          {"",  "cwds", "retrieve the current working directories of all available NST terminals one per line to stdout"},
		set_theme         {"",  "theme", "change the active theme", false, "", "theme name"},
		instance          {"p", "pid", "target the NST instance running at the given PID, ignores the NST_IPC_ADDR environment variable", false, "", "process ID", *this} {
	m_xor_group.add(save_snapshot);
	m_xor_group.add(get_snapshot);
	m_xor_group.add(get_history);
	m_xor_group.add(get_global_history);
	m_xor_group.add(test_connection);
	m_xor_group.add(get_cwds);
	m_xor_group.add(set_theme);
	this->add(m_xor_group);
}

/// IPC client utility for nst.
/**
 * This utility allows to connect to nst terminal instances and to access
 * their IPC features.
 **/
class IpcClient :
		public cosmos::MainPlainArgs {
protected: // types

	using Message = IpcHandler::Message;

protected: // functions

	cosmos::ExitStatus main(const int argc, const char **argv) override;

	/// Perform a request against the NST instances found in the environment.
	void doSingleInstanceRequest();

	/// Perform a request against the given specific NST instance.
	void doInstanceRequest(const std::string_view addr);

	void doInstanceRequest(cosmos::UnixConnection &connection);

	cosmos::UnixConnection connectSingleInstance();

	/// Receives the request status result (initial reply data)..
	cosmos::ExitStatus receiveStatus(cosmos::UnixConnection &connection);

	/// Receives data after a request has been dispatched.
	void receiveData(const Message request, cosmos::UnixConnection &connection, std::ostream &out = std::cout);

	/// Returns the UNIX address of the active NST instance as found in the environment.
	std::string_view activeInstanceAddr() const;

	/// Returns the UNIX address of the instance selected on the command line.
	std::string_view selectedInstanceAddr() const;

	/// Looks up available NST instances and returns their UNIX addresses.
	std::set<std::string> gatherGlobalInstances() const;

protected: // data

	Cmdline m_cmdline;
	cosmos::ExitStatus m_status = cosmos::ExitStatus::SUCCESS;
	std::set<std::string> m_cwds;

	static constexpr cosmos::ExitStatus CONN_ERR{2};
	static constexpr cosmos::ExitStatus RPC_ERR{3};
	static constexpr cosmos::ExitStatus INT_ERR{5};
};

cosmos::ExitStatus IpcClient::main(const int argc, const char **argv) {
	m_cmdline.parse(argc, argv);

	if (m_cmdline.get_global_history.isSet() || m_cmdline.get_cwds.isSet()) {
		for (const auto &addr: gatherGlobalInstances()) {
			doInstanceRequest(addr);
		}

		if (m_cmdline.get_cwds.isSet()) {
			for (const auto &cwd: m_cwds) {
				std::cout << cwd << "\n";
			}
		}

		return m_status;
	} else {
		doSingleInstanceRequest();
		return m_status;
	}
}

void IpcClient::doSingleInstanceRequest() {
	const auto request = [this]() -> Message {
		if (m_cmdline.save_snapshot.isSet())
			return Message::SNAPSHOT_HISTORY;
		else if (m_cmdline.get_snapshot.isSet())
			return Message::GET_SNAPSHOT;
		else if (m_cmdline.get_history.isSet())
			return Message::GET_HISTORY;
		else if (m_cmdline.test_connection.isSet())
			return Message::PING;
		else if (m_cmdline.set_theme.isSet())
			return Message::SET_THEME;
		else {
			throw INT_ERR;
		}
	}();

	auto connection = connectSingleInstance();
	connection.send(&request, sizeof(request));

	if (request == Message::SET_THEME) {
		const auto &theme = m_cmdline.set_theme.getValue();
		connection.send(theme.c_str(), theme.size() + 1);
	}

	if (receiveStatus(connection) != cosmos::ExitStatus::SUCCESS) {
		m_status = RPC_ERR;
	}

	receiveData(request, connection, m_status == cosmos::ExitStatus::SUCCESS ? std::cout : std::cerr);
}

std::string_view IpcClient::activeInstanceAddr() const {
	constexpr auto envvar = "NST_IPC_ADDR";

	auto addr = cosmos::proc::get_env_var(envvar);

	if (!addr) {
		if (!m_cmdline.test_connection.isSet()) {
			std::cerr << "Environment variable '" << envvar << "' is not set. Cannot connect to nst.\n";
		}

		throw CONN_ERR;
	}

	return addr->view();
}

std::string_view IpcClient::selectedInstanceAddr() const {
	const auto needle = std::string{'-'} + m_cmdline.instance.getValue();

	for (const auto &addr: gatherGlobalInstances()) {
		if (cosmos::is_suffix(addr, needle))
			return addr;
	}

	if (!m_cmdline.test_connection.isSet()) {
		std::cerr << "No NST instance for PID " << m_cmdline.instance.getValue() << " found.\n";
	}

	throw CONN_ERR;
}

cosmos::UnixConnection IpcClient::connectSingleInstance() {
	const auto ipc_addr = m_cmdline.instance.isSet() ?
		selectedInstanceAddr() : activeInstanceAddr();

	try {
		cosmos::UnixSeqPacketClientSocket sock;
		return sock.connect(cosmos::UnixAddress{ipc_addr, cosmos::UnixAddress::Abstract{true}});
	} catch (const cosmos::ApiError &error) {
		if (!m_cmdline.test_connection.isSet()) {
			std::cerr << "Failed to connect to nst (address: @" << ipc_addr << "): " << error.what() << "\n";
		}

		throw CONN_ERR;
	}
}

std::set<std::string> IpcClient::gatherGlobalInstances() const {
	// look up all active UNIX domain sockets matching our name pattern
	constexpr auto path = "/proc/net/unix";
	std::ifstream proc_unix;
	proc_unix.open(path);

	if (!proc_unix) {
		cosmos_throw (cosmos::FileError(path, "open"));
	}

	std::string line;
	std::set<std::string> ret;

	while (std::getline(proc_unix, line)) {
		const auto pos = line.find("@nst-ipc");
		if (pos == line.npos)
			continue;

		// the name is the last field of the output, so simply use
		// the rest of the line, but without the leading @
		ret.insert(line.substr(pos + 1));
	}

	return ret;
}

void IpcClient::doInstanceRequest(const std::string_view addr) {
	std::optional<cosmos::UnixConnection> conn;

	try {
		cosmos::UnixSeqPacketClientSocket sock;
		conn = sock.connect(cosmos::UnixAddress{addr, cosmos::UnixAddress::Abstract{true}});
	} catch (const cosmos::ApiError &error) {

		// ignore errors that are to be expected:
		// - the socket belongs to another user and we lack access
		// - the socket disappeared meanwhile
		switch (error.errnum()) {
			case cosmos::Errno::PERMISSION:
			case cosmos::Errno::ACCESS:
			case cosmos::Errno::CONN_REFUSED:
				return;
			default:
				std::cerr << "failed to connect to " << addr << ": " << error.what() << "\n";
				return;
		}
	}

	try {
		return doInstanceRequest(*conn);
	} catch (const cosmos::ApiError &error) {
		std::cerr << "error talking to " << addr << ": " << error.what() << "\n";
	}
}

void IpcClient::doInstanceRequest(cosmos::UnixConnection &connection) {
	const auto request = [this]() -> Message {
		if (m_cmdline.get_global_history.isSet())
			return Message::GET_HISTORY;
		else if (m_cmdline.get_cwds.isSet())
			return Message::GET_CWD;
		else {
			throw INT_ERR;
		}
	}();

	connection.send(&request, sizeof(request));

	if (receiveStatus(connection) != cosmos::ExitStatus::SUCCESS) {
		m_status = RPC_ERR;
		// an error message might follow
		receiveData(request, connection, std::cerr);
		return;
	}

	try {
		if (request == Message::GET_CWD) {
			std::stringstream ss;
			receiveData(request, connection, ss);
			auto cwd = ss.str();
			if (!cwd.empty()) {
				m_cwds.insert(ss.str());
			}
		} else {
			receiveData(request, connection, std::cout);
		}
	} catch (const cosmos::ApiError &error) {
		if (error.errnum() == cosmos::Errno::CONN_RESET)
			// this means the socket belongs to a different user
			// and the other nst rejected access - ignore.
			return;

		throw;
	}
}

cosmos::ExitStatus IpcClient::receiveStatus(cosmos::UnixConnection &connection) {
	cosmos::ExitStatus status;

	const auto len = connection.receive(&status, sizeof(status), cosmos::MessageFlags{cosmos::MessageFlag::TRUNCATE});

	if (len != sizeof(status)) {
		std::cerr << "received bad status code message length\n";
		throw INT_ERR;
	}

	return status;
}

void IpcClient::receiveData(const Message request, cosmos::UnixConnection &connection, std::ostream &out) {
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
			// simply forward data to the stream
			out << buffer;
		}
	}
}

} // end ns

int main(int argc, const char **argv) {
	return cosmos::main<nst::IpcClient>(argc, argv);
}
