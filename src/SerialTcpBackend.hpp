#pragma once

#include <functional>

#include <boost/asio.hpp>

#include "utils/CSingleton.hpp"
#include <string>

#ifdef _WIN32
#include <boost/asio/co_spawn.hpp>
#include <boost/asio/detached.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/signal_set.hpp>
#include <boost/asio/write.hpp>
#include "interface/ISettingsBinding.hpp"
#include <iosfwd>
#include <string_view>
using boost::asio::ip::tcp;
using boost::asio::use_awaitable_t;
using tcp_acceptor = use_awaitable_t<>::as_default_on_t<tcp::acceptor>;
using tcp_socket = use_awaitable_t<>::as_default_on_t<tcp::socket>;
namespace this_coro = boost::asio::this_coro;
#endif

class SerialTcpBackend : public CSingleton < SerialTcpBackend >, public ISettingsBinding
{
    friend class CSingleton < SerialTcpBackend >;

public:
    // ISettingsBinding - this subsystem owns its own block of settings.ini.
    [[nodiscard]] std::string_view SettingsSection() const override { return "COM_TcpBackend"; }
    void LoadSettings(SettingsReader& reader) override;
    void SaveSettings(std::ostream& out) const override;

    SerialTcpBackend();
    ~SerialTcpBackend();

    void Init();

    // !\brief Enabled?
    bool is_enabled = false;

    // !\brief Listening IP address (0.0.0.0 = any, by default)
    std::string bind_ip = "0.0.0.0";

    // !\brief Listening port (if no port-forwarding or redirecting happens, should be same as "RemoteTcpPort" config entry)
    uint16_t tcp_port = 10000;

    // !\brief What arrives on this socket is handed on as though it had come
    // off the serial port. Supplied by the composition root - this ran on an
    // asio coroutine and reached for the port's singleton to deliver it.
    void SetReceptionSink(std::function<void(const char*, unsigned int)> sink)
    {
        m_Reception = std::move(sink);
    }

private:
    std::function<void(const char*, unsigned int)> m_Reception;

#ifdef _WIN32
    boost::asio::awaitable<void> echo(tcp_socket socket);
    
    boost::asio::awaitable<void> listener();
#endif
    // !\brief ASIO IO Context
    boost::asio::io_context io_context;

    // !\brief Worker thread
    std::unique_ptr<std::jthread> m_worker = nullptr;

};