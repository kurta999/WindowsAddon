#pragma once

#include <atomic>
#include <mutex>
#include <utility>

enum class SerialPortConnectionState : unsigned char
{
    Disconnected,
    Connecting,
    Connected,
    Error,
};

class SerialPortConnectionStatus
{
public:
    SerialPortConnectionState Load() const noexcept { return m_state.load(std::memory_order_acquire); }
    void Store(SerialPortConnectionState state) noexcept { m_state.store(state, std::memory_order_release); }

private:
    std::atomic<SerialPortConnectionState> m_state{ SerialPortConnectionState::Disconnected };
};

template<typename Probe>
SerialPortConnectionState ProbeSerialPortConnectionStatusNonBlocking(
    std::mutex& transport_mutex, SerialPortConnectionStatus& cached_status, Probe&& probe)
{
    std::unique_lock lock(transport_mutex, std::try_to_lock);
    if(!lock.owns_lock())
        return cached_status.Load();
    const auto status = std::forward<Probe>(probe)();
    cached_status.Store(status);
    return status;
}
