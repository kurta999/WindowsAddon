#include "pch_core.hpp"
#include "Session.hpp"
#include "Logger.hpp"
#include "Utils.hpp"
#include "Server.hpp"

Session::Session(boost::asio::io_context& io_service, std::mutex& io_mutex,
	std::unique_ptr<ITcpMessageExecutor>&& executor, Server* owner) :
	sessionSocket(io_service), m_IoMutex(io_mutex), transferTimer(io_service), m_msgExecutor(std::move(executor)),
	m_owner(owner)
{

}

Session::~Session()
{

}

void Session::HandleRead(const boost::system::error_code& error, std::size_t bytesTransferred)
{
	if(error)
		return;

	if(bytesTransferred > receivedData.size() - receivedLength)
	{
		LOG(LogLevel::Error, "TCP receive length {} exceeds remaining buffer capacity {}",
			bytesTransferred, receivedData.size() - receivedLength);
		std::scoped_lock lock(m_IoMutex);
		StopAsync();
		return;
	}
	receivedLength += bytesTransferred;

	auto message = tcp_message::BoundedMessage(receivedData, receivedLength);

	/* Deliberately not holding m_IoMutex here: the executor can block on
	   forwarding sockets and file reads, and that mutex also gates accepts and
	   broadcasts for every other session. */
	auto ret_val = m_msgExecutor->Process(shared_from_this(), message);

	const bool should_close = std::get<0>(ret_val);
	const bool recognized = std::get<1>(ret_val);
	const std::string& response = std::get<2>(ret_val);

	std::scoped_lock lock(m_IoMutex);
	if(!response.empty())
	{
		SendAsync(response);
		if(should_close)
			is_close_pending = true;
		return;
	}

	/* Nothing matched yet and there is still room: the request may simply have
	   been split across TCP segments, so read the rest before giving up. */
	if(!recognized && receivedLength < receivedData.size() && sessionSocket.is_open())
	{
		sessionSocket.async_read_some(
			boost::asio::buffer(receivedData.data() + receivedLength, receivedData.size() - receivedLength),
			std::bind(&Session::HandleRead, shared_from_this(), std::placeholders::_1, std::placeholders::_2));
		return;
	}

	if(should_close)
		StopAsync();
}

void Session::HandleTransferTimer(const boost::system::error_code& error)
{
	std::scoped_lock lock(m_IoMutex);
	if(!error)
	{
		if(!pendingMessages.empty())
		{
			SendAsync(pendingMessages.front());
			pendingMessages.pop();
		}
		else
		{
			if(is_close_pending)
			{
				StopAsync();
			}
		}
	}
}

void Session::HandleWrite(const boost::system::error_code& error)
{
	writeInProgress = false;
	if(!error)
	{
		if(!pendingMessages.empty())
		{
			SendAsync(pendingMessages.front());
			pendingMessages.pop();
		}
		else
		{
			if(is_close_pending)
			{
				StopAsync();
			}
		}
	}
	else
	{
		pendingMessages = std::queue<std::string>();
	}
}

void Session::SendAsync(const std::string& buffer)
{
	if(writeInProgress)
	{
		pendingMessages.push(buffer);
		transferTimer.expires_after(std::chrono::milliseconds(100));
		transferTimer.async_wait(std::bind(&Session::HandleTransferTimer, shared_from_this(), std::placeholders::_1));
	}
	else
	{
		sentData = buffer;
		writeInProgress = true;
		boost::asio::async_write(sessionSocket, boost::asio::buffer(sentData, sentData.length()), 
			std::bind(&Session::HandleWrite, shared_from_this(), std::placeholders::_1));
	}
}

void Session::StartAsync()
{
	boost::system::error_code error;
	boost::asio::ip::tcp::endpoint remoteEndpoint = sessionSocket.remote_endpoint(error);
	if(error)
	{
		StopAsync();
		return;
	}

	sessionAddress = remoteEndpoint.address().to_string(); 
	sessionPort = remoteEndpoint.port();

	receivedLength = 0;
	sessionSocket.async_read_some(boost::asio::buffer(receivedData),
		std::bind(&Session::HandleRead, shared_from_this(), std::placeholders::_1, std::placeholders::_2));

	sessionIntAddr = remoteEndpoint.address().to_v4().to_uint();
}

void Session::StopAsync(bool remove_from_session_list)
{
	if(sessionSocket.is_open())
	{
		boost::system::error_code error;
		sessionSocket.shutdown(boost::asio::ip::tcp::socket::shutdown_both, error);
		sessionSocket.close(error);
		

		if(remove_from_session_list)
		{
			/* Erase by identity. Matching on the address string removed whichever
			   session happened to share this IP - so a second connection from the
			   same peer evicted a live session and leaked this one. */
			if(m_owner != nullptr)
				m_owner->ForgetSession(shared_from_this());
		}
	}
}
