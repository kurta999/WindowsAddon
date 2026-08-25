#pragma once

#include <array>
#include <boost/asio.hpp>

#include <fstream>
#include <map>
#include <queue>
#include <set>
#include <string>
#include <vector>
#include <boost/asio/steady_timer.hpp>

#include "TcpMessageExecutor.hpp"
#include "TcpMessageParser.hpp"

class Server;

class Session : public std::enable_shared_from_this<Session>
{
	friend class Server;
public:
	// !\brief `owner` is told when this session closes, so it can drop it from
	// its set. The session used to erase itself through Server::Get(), which is
	// the only reason a connection needed to know the server was a singleton.
	Session(boost::asio::io_context& io_service, std::mutex& io_mutex,
		std::unique_ptr<ITcpMessageExecutor>&& executor, Server* owner);

	~Session();

	// !\brief Send async message
	// !\param buffer [in] String buffer to send
	void SendAsync(const std::string& buffer);

	// !\brief Start async operations
	void StartAsync();

	// !\brief Stop async operations
	// !\param remove_from_session_list [in] Remove this session from session list?
	void StopAsync(bool remove_from_session_list = true);

	// !\brief Read handler for async_read_some
	// !\param error [in] Boost error code
	// !\param transferredBytes [in] Number of bytes received
	void HandleRead(const boost::system::error_code& error, std::size_t transferredBytes);

	// !\brief Handler for ASIO Deadline Timer
	// !\param error [in] Boost error code
	void HandleTransferTimer(const boost::system::error_code& error);

	// !\brief Write handler for async_write
	// !\param error [in] Boost error code
	void HandleWrite(const boost::system::error_code& error);

	// !\brief Pending messages waiting to be sent
	std::queue<std::string> pendingMessages;

	// !\brief Buffer for received data
	std::array<char, tcp_message::MaxMessageSize> receivedData{};

	// !\brief Bytes accumulated in receivedData across partial reads
	std::size_t receivedLength = 0;

	// !\brief Last sent data
	std::string sentData;

	// !\brief IP Address of session
	std::string sessionAddress;

	// !\brief IP Address of session (in integer form)
	int sessionIntAddr;

	// !\brief Port of session
	unsigned short sessionPort = 0;

	// !\brief ASIO TCP socket of session
	boost::asio::ip::tcp::socket sessionSocket;

	// !\brief Mutex for IO operations
	std::mutex& m_IoMutex;

	// !\brief Is write in progress?
	bool writeInProgress = false;

	// !\brief Is session close pending?
	bool is_close_pending = false;

	// !\brief ASIO steady timer for sending message chunks
	boost::asio::steady_timer transferTimer;
	
	// !\brief Pointer to TCP message executor
	std::unique_ptr<ITcpMessageExecutor> m_msgExecutor;
	Server* m_owner = nullptr;
};
