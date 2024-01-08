// C++
#include <iostream>

// cosmos
#include "cosmos/error/ApiError.hxx"
#include "cosmos/proc/process.hxx"

// nst
#include "IpcHandler.hxx"
#include "nst.hxx"

namespace nst {


namespace {
	auto& log_error() {
		return std::cerr << "nst: IPC: ";
	}
}

std::string IpcHandler::address() {
	std::string ret{"nst-ipc-"};
	const auto pid = cosmos::proc::cached_pids.own_pid;

	ret.append(std::to_string(cosmos::to_integral(pid)));
	return ret;
}

void IpcHandler::init() {
	m_listener.bind(cosmos::UnixAddress{address(), cosmos::UnixAddress::AbstractAddress{true}});
	m_listener.listen(5);
	m_poller.addFD(m_listener.fd(), {cosmos::Poller::MonitorFlag::INPUT});
}

void IpcHandler::checkEvent(const cosmos::Poller::PollEvent &event) {
	if (m_state == State::WAITING) {
		if (event.fd() == m_listener.fd()) {
			try {
				acceptConnection();
			} catch (const cosmos::ApiError &e) {
				log_error() << e.what() << "\n";
			}
		}
	} else if (event.fd() == m_connection->fd()) {
		switch (m_state) {
			case State::RECEIVING:
				receiveCommand();
				break;
			case State::SENDING:
				sendData();
				break;
			default:
				log_error() << "bad IPC state, closing session\n";
				closeSession();
				break;
		}
	}
}

void IpcHandler::acceptConnection() {
	m_connection = m_listener.accept();

	auto opts = m_connection->unixOptions();
	if (auto peer_uid = opts.credentials().userID(); peer_uid != cosmos::proc::get_real_user_id()) {
		log_error() << "rejecting connection from uid " << cosmos::to_integral(peer_uid) << "\n";
		m_connection->close();
		m_connection.reset();
		return;
	}

	m_poller.delFD(m_listener.fd());
	m_poller.addFD(m_connection->fd(), {cosmos::Poller::MonitorFlag::INPUT});
	m_state = State::RECEIVING;
}

void IpcHandler::receiveCommand() {
	auto &connection = *m_connection;
	Message message = Message::INVALID;

	size_t len = 0;

	try {
		len = connection.receive(&message, sizeof(Message), cosmos::MessageFlags{cosmos::MessageFlag::TRUNCATE});
	} catch (const cosmos::ApiError &error) {
		log_error() << "receive error: " << error.what() << "\n";
		closeSession();
		return;
	}

	if (len != sizeof(Message)) {
		if (len < sizeof(Message))
			log_error() << "short IPC message, closing session.\n";
		else
			log_error() << "too long (truncated) IPC message, closing session.\n";
		closeSession();
		return;
	}

	processCommand(message);

	if (m_state == State::SENDING) {
		// transitioned to sending, we need to monitor output now
		m_poller.modFD(connection.fd(), {cosmos::Poller::MonitorFlag::OUTPUT});
	}
}

std::string IpcHandler::history() const {
	const auto &term = m_nst.term();
	auto ret = term.screen().asText(term.cursor());

	if (!term.onAltScreen()) {
		// drop the last line which contains the currently entered
		// command line. this avoids that e.g.
		//     nst-msg -d | grep something
		// matches the very command line that searches for `something`.
		// TODO: this is currently not unicode safe ...
		auto pos = ret.find_last_of('\n', ret.size() >= 2 ? ret.size() - 2 : ret.npos);
		if (pos != ret.npos) {
			ret.erase(pos+1);
		}
	}

	return ret;
}

void IpcHandler::processCommand(const Message message) {
	switch (message) {
		default:
			log_error() << "bad request received: " << cosmos::to_integral(message) << "\n";
			closeSession();
			return;
		case Message::SNAPSHOT_HISTORY:
			m_snapshot = history();
			closeSession();
			return;
		case Message::GET_HISTORY:
			m_send_data = history();
			break;
		case Message::GET_SNAPSHOT:
			m_send_data = m_snapshot;
			break;
		case Message::PING:
			constexpr auto msg = Message::PING;
			m_send_data.resize(sizeof(msg));
			std::memcpy(m_send_data.data(), &msg, sizeof(msg));
			break;
	}

	m_state = State::SENDING;
}

void IpcHandler::sendData() {
	try {
		const auto to_send = std::min(m_send_data.size() - m_send_pos, MAX_CHUNK_SIZE);
		const auto sent = m_connection->send(m_send_data.data() + m_send_pos, to_send);
		m_send_pos += sent;

		if (m_send_pos >= m_send_data.size()) {
			closeSession();
		}
	} catch(const cosmos::ApiError &e) {
		log_error() << "failed to send IPC message: " << e.what() << ". Closing session.\n";
		closeSession();
	}
}

void IpcHandler::closeSession() {
	m_state = State::WAITING;

	if (!m_connection)
		return;

	m_poller.addFD(m_listener.fd(), {cosmos::Poller::MonitorFlag::INPUT});
	m_poller.delFD(m_connection->fd());
	m_connection->close();
	m_connection.reset();
	m_send_data.clear();
	m_send_pos = 0;
}

} // end ns
