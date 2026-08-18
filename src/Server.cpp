#include "pch.hpp"

Server::Server()
{

}

Server::~Server()
{
    StopAsync();
    if(m_worker)
        m_worker.reset(nullptr);
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
        m_worker = std::make_unique<std::jthread>(std::bind_front(&Server::StartAsync, this));
        if(m_worker)
            utils::SetThreadName(*m_worker, "Server");
        DatabaseLogic::Get()->GenerateGraphs(Sensors::Get()->GetGraphResolution());
    }
}

void Server::StartAsync(std::stop_token token)
{
    unsigned short port = acceptor->local_endpoint().port();
    io_service.restart();
    boost::system::error_code error;
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

void Server::SetForwardIpAddress(const std::string& ip)
{
    std::scoped_lock lock(m_StateMutex);
    SetForwardEndpoint(ip, m_forwardEndpoint);
}

void Server::SetForwardIpAddress2(const std::string& ip)
{
    std::scoped_lock lock(m_StateMutex);
    SetForwardEndpoint(ip, m_forwardEndpoint2);
}

const std::string Server::GetForwardIpAddress()
{
    std::scoped_lock lock(m_StateMutex);
    return FormatForwardEndpoint(m_forwardEndpoint);
}

const std::string Server::GetForwardIpAddress2()
{
    std::scoped_lock lock(m_StateMutex);
    return FormatForwardEndpoint(m_forwardEndpoint2);
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

    try
    {
        const auto parsed_port = std::stoul(value.substr(separator + 1));
        if(parsed_port > std::numeric_limits<uint16_t>::max())
            throw std::out_of_range("port");
        endpoint = {value.substr(0, separator), static_cast<uint32_t>(parsed_port)};
    }
    catch(const std::exception& error)
    {
        LOG(LogLevel::Error, "Invalid forwarding endpoint '{}': {}", value, error.what());
    }
}

std::string Server::FormatForwardEndpoint(const ForwardEndpoint& endpoint) const
{
    return endpoint.port == 0 ? "null" : endpoint.address + ":" + std::to_string(endpoint.port);
}

std::vector<ForwardEndpoint> Server::GetForwardTargets() const
{
    std::scoped_lock lock(m_StateMutex);
    std::vector<ForwardEndpoint> targets;
    if(m_forwardEndpoint.port != 0)
        targets.push_back(m_forwardEndpoint);
    if(m_forwardEndpoint2.port != 0)
        targets.push_back(m_forwardEndpoint2);
    return targets;
}

std::vector<uint32_t> Server::GetConnectedSensorAddresses() const
{
    std::scoped_lock lock(m_IoMutex);
    return {m_usedIpAddresses.begin(), m_usedIpAddresses.end()};
}

bool Server::CreateAcceptor(unsigned short port)
{
    boost::system::error_code error;
    boost::asio::ip::tcp::endpoint endpoint(boost::asio::ip::tcp::v4(), port);
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

void Server::StartAccept()
{
    SharedSession session = std::make_shared<Session>(io_service, m_IoMutex, std::make_unique<TcpMessageExecutor>());
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
                LOG(LogLevel::Error, "New sensor connected, IP: {}:{}", session->sessionAddress, session->sessionPort);
            }
            StartAccept();
        }
    }
}
