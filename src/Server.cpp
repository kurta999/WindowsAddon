#include "pch_core.hpp"
#include "Server.hpp"
#include "Logger.hpp"
#include "Utils.hpp"
#include "SettingsReader.hpp"
#include "SettingsWriter.hpp"
#include "utils/XmlDocument.hpp"

Server::Server()
{

}

Server::~Server()
{
    StopAsync();
    if(m_worker)
        m_worker.reset(nullptr);
}

void Server::LoadSettingsFrom(SettingsReader& reader, std::string_view section)
{
    const std::string block(section);
    SetEnabled(utils::stob(reader.Required(block, "Enable")));
    SetPort(utils::stoi<uint16_t>(reader.Required(block, "TCP_Port")));

    /* Optional so existing settings.ini files keep working. When it is absent
       the server stays on loopback, which is the safe default. */
    std::string listening_ip = "127.0.0.1";
    if(auto values = reader.OptionalSection(block))
        utils::ini::ReadValueIfexists(values, "ListeningIp", listening_ip);
    SetBindAddress(listening_ip);

    SetForwardIpAddress(0, reader.Required(block, "MeasurementForward"));
    SetForwardIpAddress(1, reader.Required(block, "MeasurementForward2"));
}

void Server::WriteSettingsTo(SettingsWriter& writer) const
{
    writer.Key("Enable", IsEnabled(), "Toggle TCP server")
        .Key("TCP_Port", GetPort(), "TCP Port for receiving measurements from sensors")
        .Comment("Interface to bind to. Keep 127.0.0.1 unless sensors live on other machines:")
        .Comment("the backend accepts commands that open Explorer windows and has no authentication.")
        .Key("ListeningIp", GetBindAddress());
}

void Server::WriteForwardSettingsTo(SettingsWriter& writer) const
{
    writer.Key("MeasurementForward", GetForwardIpAddress(0))
        .Key("MeasurementForward2", GetForwardIpAddress(1));
}

void Server::Init(void)
{
    if(IsEnabled())
    {
        std::scoped_lock lock(m_IoMutex);
        if(!CreateAcceptor(GetPort()))
        {
            LOG(LogLevel::Error, "createAcceptor fail!");
            return;
        }
        m_worker = utils::StartNamedWorker("Server", std::bind_front(&Server::StartAsync, this));
        if(m_GraphRefresh)
            m_GraphRefresh();
    }
}

void Server::StartAsync(std::stop_token)
{
    /* Nothing here may touch the acceptor. StopAsync can already have reset
       it by the time this thread starts running, and reading its endpoint
       then either dereferences a null shared_ptr or throws out of a jthread.
       The io_context is all this worker needs. */
    io_service.restart();
    io_service.run();
    LOG(LogLevel::Verbose, "tcp backend io_service finish");
}

void Server::BroadcastMessage(const std::string& msg)
{
    if(IsEnabled())
    {
        std::scoped_lock lock(m_IoMutex);
        for(auto& i : sessions)
        {
            i->SendAsync(msg);
        }
    }
}

void Server::SetForwardIpAddress(size_t index, const std::string& ip)
{
    std::scoped_lock lock(m_StateMutex);
    SetForwardEndpoint(ip, m_forwardEndpoints.at(index));
}

std::string Server::GetForwardIpAddress(size_t index) const
{
    std::scoped_lock lock(m_StateMutex);
    return FormatForwardEndpoint(m_forwardEndpoints.at(index));
}

void Server::SetForwardEndpoint(const std::string& value, ForwardEndpoint& endpoint)
{
    endpoint = {};
    if(value == "null" || value.empty())
        return;

    const size_t separator = value.rfind(':');
    if(separator == std::string::npos)
    {
        LOG(LogLevel::Error, "Invalid forwarding endpoint: {}", value);
        return;
    }

    /* TryParse range-checks against uint16_t itself, so the throw-to-report-a
       bad-range trick this used goes with the try block. */
    const std::optional<uint16_t> parsed_port = utils::TryParse<uint16_t>(value.substr(separator + 1));
    if(!parsed_port)
    {
        LOG(LogLevel::Error, "Invalid forwarding endpoint '{}': port must be a number in 0-65535", value);
        return;
    }

    endpoint = {value.substr(0, separator), static_cast<uint32_t>(*parsed_port)};
}

std::string Server::FormatForwardEndpoint(const ForwardEndpoint& endpoint) const
{
    return endpoint.port == 0 ? "null" : endpoint.address + ":" + std::to_string(endpoint.port);
}

std::vector<ForwardEndpoint> Server::GetForwardTargets() const
{
    std::scoped_lock lock(m_StateMutex);
    std::vector<ForwardEndpoint> targets;
    for(const ForwardEndpoint& endpoint : m_forwardEndpoints)
    {
        if(endpoint.port != 0)
            targets.push_back(endpoint);
    }
    return targets;
}

std::vector<uint32_t> Server::GetConnectedSensorAddresses() const
{
    std::scoped_lock lock(m_IoMutex);
    return {m_usedIpAddresses.begin(), m_usedIpAddresses.end()};
}

void Server::SetBindAddress(const std::string& address)
{
    std::scoped_lock lock(m_StateMutex);
    m_bindAddress = address.empty() ? std::string("127.0.0.1") : address;
}

std::string Server::GetBindAddress() const
{
    std::scoped_lock lock(m_StateMutex);
    return m_bindAddress;
}

bool Server::CreateAcceptor(unsigned short port)
{
    boost::system::error_code error;

    /* This listener accepts commands that open Explorer windows, so it stays on
       loopback unless settings.ini names another interface. An unparseable
       address falls back to loopback rather than silently widening the bind. */
    const std::string bind_address = GetBindAddress();
    auto address = boost::asio::ip::make_address(bind_address, error);
    if(error)
    {
        LOG(LogLevel::Error, "Invalid ListeningIp '{}' ({}), falling back to 127.0.0.1",
            bind_address, error.message());
        address = boost::asio::ip::make_address("127.0.0.1");
        error.clear();
    }
    if(!address.is_loopback())
    {
        LOG(LogLevel::Warning, "TCP backend is bound to {} - it is reachable from the network "
            "and has no authentication. Use 127.0.0.1 unless you need remote sensors.",
            address.to_string());
    }

    boost::asio::ip::tcp::endpoint endpoint(address, port);
    acceptor = std::make_shared<boost::asio::ip::tcp::acceptor>(io_service);
    acceptor->open(endpoint.protocol(), error);
    if(error)
    {
        LOG(LogLevel::Error, "open error {} - {}", port, error.message());
        acceptor.reset();
        return false;
    }

    acceptor->set_option(boost::asio::ip::tcp::acceptor::reuse_address(true), error);
    if(error)
    {
        LOG(LogLevel::Error, "reuse_address error - {}", error.message());
        acceptor.reset();
        return false;
    }
    acceptor->bind(endpoint, error);
    if(error)
    {
        LOG(LogLevel::Error, "bind error - {}", error.message());
        acceptor.reset();
        return false;
    }
    acceptor->listen(boost::asio::socket_base::max_listen_connections, error);
    if(error)
    {
        LOG(LogLevel::Error, "listen error - {}", error.message());
        acceptor.reset();
        return false;
    }
    StartAccept();
    m_isOk = true;
    return true;
}

void Server::StopAsync()
{
    if(IsEnabled())
    {
        std::scoped_lock lock(m_IoMutex);
        if(acceptor)
        {
            acceptor->close();
            acceptor.reset();

            for(const auto& c : sessions)
            {
                c->StopAsync(false);
            }
            sessions.clear();
        }
        io_service.stop();
    }
}

void Server::ForgetSession(const SharedSession& session)
{
    sessions.erase(session);
}

void Server::StartAccept()
{
    if(m_Measurements == nullptr)
    {
        LOG(LogLevel::Error, "No measurement sink wired to the TCP server, not accepting connections");
        return;
    }

    SharedSession session = std::make_shared<Session>(io_service, m_IoMutex,
        std::make_unique<TcpMessageExecutor>(*m_Measurements, m_SharedDriveLetter), this);
    acceptor->async_accept(session->sessionSocket, std::bind(&Server::HandleAccept, this, std::placeholders::_1, session));
}

void Server::HandleAccept(const boost::system::error_code& error, SharedSession session)
{
    std::scoped_lock lock(m_IoMutex);
    if(acceptor)
    {
        if(!error)
        {
            session->StartAsync();
            sessions.emplace(session);
            if(m_usedIpAddresses.emplace(session->sessionIntAddr).second)
            {

                LOG(LogLevel::Notification, "New sensor connected, IP: {}:{}", session->sessionAddress, session->sessionPort);
            }
            StartAccept();
        }
    }
}
