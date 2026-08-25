#pragma once

#include "utils/CSingleton.hpp"

#include "Settings.hpp"
#include <boost/asio.hpp>

#include <inttypes.h>
#include <array>
#include <map>
#include <string>
#include <deque>

#include "Session.hpp"
#include <thread>
#include <atomic>
#include <vector>

typedef std::shared_ptr<boost::asio::ip::tcp::acceptor> SharedAcceptor;
typedef std::shared_ptr<Session> SharedSession;

class IMeasurementSink;
class SettingsReader;
class SettingsWriter;

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
    
    // !\brief How many forwarding targets a server carries.
    static constexpr size_t kForwardTargets = 2;

    // !\brief Set forwarding target `index` from an "ip:port" string.
    //
    // Was SetForwardIpAddress / SetForwardIpAddress2 and the matching pair of
    // getters - four wrappers whose bodies differed by one character, over two
    // members whose names differed the same way. GetForwardTargets already
    // treated them as the list they are.
    void SetForwardIpAddress(size_t index, const std::string& ip);

    // !\brief Forwarding target `index` as "ip:port", or "null" when unset.
    [[nodiscard]] std::string GetForwardIpAddress(size_t index) const;

    // !\brief Read and write the five keys this server owns.
    //
    // They live in the [Sensors] block because that is where every settings.ini
    // in the wild has them, so Sensors' binding calls these rather than
    // reaching in through a singleton to spell them itself. Moving them to a
    // [Server] section of their own is a settings migration, not a refactor.
    void LoadSettingsFrom(SettingsReader& reader, std::string_view section);
    void WriteSettingsTo(SettingsWriter& writer) const;

    // !\brief The two forwarding keys, which the file writes after Sensors'
    // own three. Split so the block keeps the order it has always had.
    void WriteForwardSettingsTo(SettingsWriter& writer) const;

    // !\brief What to render when the listener comes up. Supplied by the
    // composition root; this used to be a DatabaseLogic::Get()->GenerateGraphs(
    // Sensors::Get()->GetGraphResolution()) chain, which is the whole cycle in
    // one line - the server reaching back into the two things that own it.
    void SetGraphRefresh(std::function<void()> refresh) { m_GraphRefresh = std::move(refresh); }

    // !\brief Called by a session that is closing, to drop it from the set.
    void ForgetSession(const SharedSession& session);

    // !\brief Where sensor broadcasts from accepted connections go.
    // Supplied by the composition root; without it the server refuses to
    // accept rather than dropping measurements silently.
    void SetMeasurementSink(IMeasurementSink& sink) noexcept { m_Measurements = &sink; }

    // !\brief Which drive letter the Explorer requests are rooted at. Relayed
    // to each connection's executor; asked per request, not per connection.
    void SetSharedDriveLetterSource(std::function<char()> source) { m_SharedDriveLetter = std::move(source); }

    void SetEnabled(bool enabled) noexcept { m_isEnabled = enabled; }
    [[nodiscard]] bool IsEnabled() const noexcept { return m_isEnabled; }
    [[nodiscard]] bool IsOk() const noexcept { return m_isOk; }

    void SetPort(uint16_t port) noexcept { m_tcpPort = port; }
    [[nodiscard]] uint16_t GetPort() const noexcept { return m_tcpPort; }

    // !\brief Interface the listener binds to. Defaults to loopback: this
    // endpoint executes shell actions, so it is not exposed off-box unless the
    // user explicitly asks for it in settings.ini.
    void SetBindAddress(const std::string& address);
    [[nodiscard]] std::string GetBindAddress() const;

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
    IMeasurementSink* m_Measurements = nullptr;
    std::function<void()> m_GraphRefresh;
    std::function<char()> m_SharedDriveLetter;
    std::atomic<bool> m_isEnabled{false};
    std::atomic<bool> m_isOk{false};
    std::atomic<uint16_t> m_tcpPort{2005};
    std::string m_bindAddress{"127.0.0.1"};
    std::array<ForwardEndpoint, kForwardTargets> m_forwardEndpoints;

    // !\brief IO Service
    boost::asio::io_context io_service;
};
