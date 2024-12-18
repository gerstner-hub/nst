// C++
#include <iostream>

// cosmos
#include "cosmos/error/ApiError.hxx"
#include "cosmos/formatting.hxx"
#include "cosmos/fs/filesystem.hxx"
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
	m_listener.bind(cosmos::UnixAddress{address(), cosmos::UnixAddress::Abstract{true}});
	m_listener.listen(5);
	m_poller.addFD(m_listener.fd(), {cosmos::Poller::MonitorFlag::INPUT});
}

bool IpcHandler::checkEvent(const cosmos::Poller::PollEvent &event) {
	bool redraw = false;

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
				redraw = receiveCommand();
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

	return redraw;
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

bool IpcHandler::receiveCommand() {
	auto &connection = *m_connection;
	Message message = Message::INVALID;

	size_t len = 0;

	try {
		len = receiveData(reinterpret_cast<char*>(&message), sizeof(Message));
	} catch (const cosmos::ApiError &error) {
		return false;
	}

	if (len != sizeof(Message)) {
		if (len < sizeof(Message))
			log_error() << "short IPC command, closing session.\n";
		else
			log_error() << "too long IPC command, closing session.\n";
		closeSession();
		return false;
	}

	const bool redraw = processCommand(message);

	if (m_state == State::SENDING) {
		// transitioned to sending, we need to monitor output now
		m_poller.modFD(connection.fd(), {cosmos::Poller::MonitorFlag::OUTPUT});
	}

	return redraw;
}

size_t IpcHandler::receiveData(char *buffer, const size_t max_size) {
	try {
		return m_connection->receive(buffer, max_size, cosmos::MessageFlags{cosmos::MessageFlag::TRUNCATE});
	} catch (const cosmos::ApiError &error) {
		log_error() << "receive error: " << error.what() << "\n";
		closeSession();
		throw;
	}
}

std::string IpcHandler::history() const {
	const auto &term = m_nst.term();
	// always operate on the main screen for two reasons:
	// - this will be the typical use case
	// - the current screen is subject to race conditions e.g. the command
	//   line `nst-msg -d | less` is subject to race conditions, since
	//   less switches to the alt screen and if that happens first, then
	//   less displays itself, so to say.
	const auto &screen = term.onAltScreen() ? term.savedScreen() : term.screen();
	const auto &cursor = term.onAltScreen() ? screen.getCachedCursor() : term.cursor();
	auto ret = screen.asText(cursor);

	// drop the last line which contains the currently entered
	// command line. this avoids that e.g.
	//     nst-msg -d | grep something
	// matches the very command line that searches for `something`.
	// TODO: this is currently not unicode safe ...
	auto pos = ret.find_last_of('\n', ret.size() >= 2 ? ret.size() - 2 : ret.npos);
	if (pos != ret.npos) {
		ret.erase(pos+1);
	}

	return ret;
}

bool IpcHandler::processCommand(const Message message) {
	bool redraw = false;
	cosmos::ExitStatus cmd_res = cosmos::ExitStatus::SUCCESS;

	switch (message) {
		default:
			log_error() << "bad request received: " << cosmos::to_integral(message) << "\n";
			cmd_res = cosmos::ExitStatus::FAILURE;
			break;
		case Message::SNAPSHOT_HISTORY:
			m_snapshot = history();
			break;
		case Message::GET_HISTORY:
			m_send_queue.emplace_back(history());
			break;
		case Message::GET_SNAPSHOT:
			m_send_queue.push_back(m_snapshot);
			break;
		case Message::GET_CWD:
			m_send_queue.emplace_back(childCWD());
			break;
		case Message::PING: {
			constexpr auto msg = Message::PING;
			auto &data = m_send_queue.emplace_back(std::string{});
			data.resize(sizeof(msg));
			std::memcpy(data.data(), &msg, sizeof(msg));
			break;
		}
		case Message::SET_THEME: {
			if (handleSetTheme()) {
				redraw = true;
			} else {
				cmd_res = cosmos::ExitStatus::FAILURE;
			}
			break;
		}
	}

	queueStatus(cmd_res);

	m_state = State::SENDING;
	return redraw;
}

bool IpcHandler::handleSetTheme() {
	std::string theme;
	theme.resize(128);

	try {
		const auto len = receiveData(theme.data(), theme.size());
		if (len == 0 || len > theme.size()) {
			log_error() << "set_theme request: excess theme name length encountered\n";
		} else {
			theme.resize(len);
			// remove trailing terminator
			if (theme.back() == '\0')
				theme.pop_back();
		}
	} catch (const cosmos::ApiError&) {
		return false;
	}

	if (!m_nst.setTheme(theme)) {
		m_send_queue.push_back("invalid theme name encountered");
		return false;
	}

	return true;
}

void IpcHandler::queueStatus(cosmos::ExitStatus status) {
	// place this at the front, the status needs to be the first message
	// sent back
	auto &data = m_send_queue.emplace_front(std::string{});
	data.resize(sizeof(status));
	std::memcpy(data.data(), &status, sizeof(status));
}

void IpcHandler::sendData() {
	const auto &data = m_send_queue.front();
	// we possibly need to chunk the payload here, because replies like
	// the history buffer can be larger than the maximum seq-packet socket
	// message length.
	const auto left = data.size() - m_msg_pos;
	const auto chunk_bytes = std::min(MAX_CHUNK_SIZE, left);

	try {
		const auto sent = m_connection->send(data.data() + m_msg_pos, chunk_bytes);

		if (sent != chunk_bytes) {
			log_error() << "short IPC message sent.\n";
			closeSession();
			return;
		}

		m_msg_pos += sent;

		if (m_msg_pos == data.size()) {
			// we're done with this message, remove it from the queue
			m_send_queue.pop_front();
			m_msg_pos = 0;
			if (m_send_queue.empty()) {
				// everything has been sent out
				closeSession();
			}
		}
	} catch (const cosmos::ApiError &e) {
		log_error() << "failed to send IPC message: " << e.what() << ". Closing session.\n";
		closeSession();
	}
}

void IpcHandler::closeSession() {
	m_state = State::WAITING;
	m_send_queue.clear();
	m_msg_pos = 0;

	if (!m_connection)
		return;

	m_poller.addFD(m_listener.fd(), {cosmos::Poller::MonitorFlag::INPUT});
	m_poller.delFD(m_connection->fd());
	m_connection->close();
	m_connection.reset();
	m_send_status = cosmos::ExitStatus::SUCCESS;
}

std::string IpcHandler::childCWD() const {
	const auto pid = m_nst.tty().childPID();
	// NOTE: this does currently not take into account possible
	// inter-mediate processes like a `scroll` helper program. In this
	// case we'd need the grandchildren CWD (the shell).
	std::string str = cosmos::sprintf(
			"/proc/%s/cwd",
			std::to_string(cosmos::to_integral(pid)).c_str());
	return cosmos::fs::read_symlink(str);
}

} // end ns
