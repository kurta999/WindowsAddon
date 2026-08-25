#include "pch_core.hpp"
#include "SerialTcpBackend.hpp"
#include "Logger.hpp"
#include "Utils.hpp"
#include "SerialPort.hpp"
#include "SettingsReader.hpp"
#include "SettingsWriter.hpp"
#include <ostream>

constexpr int LOCA_RECV_BUFFER = 1024;

#ifdef _WIN32
boost::asio::awaitable<void> SerialTcpBackend::echo(tcp_socket socket)
{
    try
    {
        char data[LOCA_RECV_BUFFER];
        for(;;)
        {
            std::size_t n = co_await socket.async_read_some(boost::asio::buffer(data));

            if(m_Reception)
                m_Reception(data, static_cast<unsigned int>(n));
        }
    }
    catch(const std::exception& e)
    {
        LOG(LogLevel::Error, "Exception: {}", e.what());
    }
}

boost::asio::awaitable<void> SerialTcpBackend::listener()
{
    auto executor = co_await boost::asio::this_coro::executor;
    /* This is a member function: the two settings it needs are its own, and
       going through the singleton for them was a habit, not a dependency. */
    tcp_acceptor acceptor(executor, { tcp::endpoint(boost::asio::ip::make_address(bind_ip), tcp_port) });
    for(;;)
    {
        auto socket = co_await acceptor.async_accept();
        boost::asio::co_spawn(executor, echo(std::move(socket)), boost::asio::detached);
    }
}
#endif // _WIN32

SerialTcpBackend::SerialTcpBackend()
{
    
}

void SerialTcpBackend::Init()
{
    if(is_enabled)
    {
        m_worker = utils::StartNamedWorker("SerialForwarder", [this] {
#ifdef _WIN32
            try
            {
                boost::asio::co_spawn(io_context, listener(), boost::asio::detached);
                io_context.run();
                LOG(LogLevel::Notification, "iocontext finish");
            }
            catch(const std::exception& e)
            {
                LOG(LogLevel::Error, "std exception: {}", e.what());
            }
            catch(...)
            {
                LOG(LogLevel::Error, "Unknown exception");
            }
#else
            LOG(LogLevel::Warning, "Serial-over-TCP forwarding is not supported on this platform");
#endif
        });
    }
}

SerialTcpBackend::~SerialTcpBackend()
{
    io_context.stop();
    if(m_worker)
        m_worker.reset(nullptr);
}

void SerialTcpBackend::LoadSettings(SettingsReader& reader)
{
    is_enabled = utils::stob(reader.Required("COM_TcpBackend", "Enable"));
    bind_ip = reader.Required("COM_TcpBackend", "ListeningIp");
    tcp_port = utils::stoi<uint16_t>(reader.Required("COM_TcpBackend", "ListeningPort"));
}

void SerialTcpBackend::SaveSettings(std::ostream& out) const
{
    SettingsWriter(out, "COM_TcpBackend")
        .Key("Enable", is_enabled, "Listening port from second instance where the TCP Forwarder forwards data received from COM port")
        .Key("ListeningIp", bind_ip)
        .Key("ListeningPort", tcp_port)
        .Blank();
}
