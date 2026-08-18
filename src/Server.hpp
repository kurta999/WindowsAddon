#pragma once

#include "utils/CSingleton.hpp"

#include "Settings.hpp"
#include <boost/asio.hpp>

#include <inttypes.h>
#include <map>
#include <string>
#include <deque>

#include "Session.hpp"
#include <thread>
#include <atomic>
#include <vector>

typedef std::shared_ptr<boost::asio::ip::tcp::acceptor> SharedAcceptor;
typedef std::shared_ptr<Session> SharedSession;

struct ForwardEndpoint
{
    std::string address;
    uint32_t port = 0;
};

class Server : public CSingleton < Server >
{
    friend class CSingleton < Server >;
    friend class Session;

public:
    Server();
    ~Server();

    // !\brief Initialize TCP backend server for sensors
    void Init();

    // !\brief Start async operations
    void StartAsync(std::stop_token token);

    // !\brief Broadcast message to every session
    // !\param msg [in] TCP Server port
    void BroadcastMessage(const std::string& msg);
    
    // !\brief Set Forward TCP Server IP & Port
    void SetForwardIpAddress(const std::string& ip);    
    
    // !\brief Set Forward 2 TCP Server IP & Port
    void SetForwardIpAddress2(const std::string& ip);

    // !\brief Get Forward TCP Server IP & Port
    const std::string GetForwardIpAddress();    
    
    // !\brief Get Forward TCP Server 2 IP & Port
    const std::string GetForwardIpAddress2();

    void SetEnabled(bool enabled) noexcept { m_isEnabled = enabled; }
    [[nodiscard]] bool IsEnabled() const noexcept { return m_isEnabled; }
    [[nodiscard]] bool IsOk() const noexcept { return m_isOk; }

    void SetPort(uint16_t port) noexcept { m_tcpPort = port; }
    [[nodiscard]] uint16_t GetPort() const noexcept { return m_tcpPort; }

    [[nodiscard]] std::vector<ForwardEndpoint> GetForwardTargets() const;
    [[nodiscard]] std::vector<uint32_t> GetConnectedSensorAddresses() const;

private:
    
    // !\brief Create TCP acceptor
    // !\param port [in] TCP Server port
    bool CreateAcceptor(unsigned short port);

    void SetForwardEndpoint(const std::string& value, ForwardEndpoint& endpoint);
    [[nodiscard]] std::string FormatForwardEndpoint(const ForwardEndpoint& endpoint) const;

    // !\brief Stop async operations
    void StopAsync();

    // !\brief Start async accept
    void StartAccept();

    // !\brief Handle async accept
    // !\param error [in] Boost error code
    // !\param session [in] Shared pointer to current session
    void HandleAccept(const boost::system::error_code& error, SharedSession session);

    // !\brief Set of active sessions
    std::set<SharedSession> sessions;

    std::set<uint32_t> m_usedIpAddresses;

    // !\brief Worker thread
    std::unique_ptr<std::jthread> m_worker = nullptr;

    // !\brief ASIO Acceptor
    SharedAcceptor acceptor;

    // !\brief Mutex for IO operations
    mutable std::mutex m_IoMutex;

    mutable std::mutex m_StateMutex;
    std::atomic<bool> m_isEnabled{true};
    std::atomic<bool> m_isOk{false};
    std::atomic<uint16_t> m_tcpPort{2005};
    ForwardEndpoint m_forwardEndpoint;
    ForwardEndpoint m_forwardEndpoint2;

    // !\brief IO Service
    boost::asio::io_context io_service;
};
