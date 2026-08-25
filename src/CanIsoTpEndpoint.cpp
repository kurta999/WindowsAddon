#include "pch_core.hpp"
#include "CanIsoTpEndpoint.hpp"
#include "Logger.hpp"
#include "Utils.hpp"

#include <algorithm>
#include <cstdarg>
#include <cstdio>
#include <limits>
#include <utility>

CanIsoTpEndpoint::CanIsoTpEndpoint(ICanTransport& transport, IClock& clock, std::uint32_t request_id) :
    m_Transport(transport), m_Clock(clock)
{
    isotp_init_link(&m_Link, request_id, m_SendBuf, sizeof(m_SendBuf), m_RecvBuf, sizeof(m_RecvBuf));
    isotp_set_callbacks(&m_Link, this, &CanIsoTpEndpoint::SendCan, &CanIsoTpEndpoint::Milliseconds);
}

int CanIsoTpEndpoint::SendCan(void* context, std::uint32_t arbitration_id,
    const std::uint8_t* data, std::uint8_t size)
{
    /* Called from inside the link, so m_Mutex is already held by whichever of
       Send, Poll or OnCanFrame is running. The transport takes its own lock and
       never reaches back into this object. */
    auto& endpoint = *static_cast<CanIsoTpEndpoint*>(context);
    endpoint.m_Transport.Send(arbitration_id, std::span<const std::uint8_t>{ data, size });
    return ISOTP_RET_OK;
}

std::uint32_t CanIsoTpEndpoint::Milliseconds(void* context)
{
    const auto& endpoint = *static_cast<CanIsoTpEndpoint*>(context);
    const auto milliseconds = std::chrono::duration_cast<std::chrono::milliseconds>(
        endpoint.m_Clock.Now().time_since_epoch()).count();
    return static_cast<std::uint32_t>(milliseconds);
}

void CanIsoTpEndpoint::SetRequestId(std::uint32_t frame_id)
{
    std::scoped_lock lock(m_Mutex);
    m_Link.send_arbitration_id = frame_id;
}

void CanIsoTpEndpoint::SetResponseId(std::uint32_t frame_id)
{
    std::scoped_lock lock(m_Mutex);
    m_ResponseId = frame_id;
}

std::uint32_t CanIsoTpEndpoint::ResponseId() const
{
    std::scoped_lock lock(m_Mutex);
    return m_ResponseId;
}

void CanIsoTpEndpoint::Send(std::uint32_t frame_id, const std::uint8_t* data, std::uint16_t size)
{
    std::scoped_lock lock(m_Mutex);
    isotp_send_with_id(&m_Link, frame_id, data, size);
}

void CanIsoTpEndpoint::Poll()
{
    std::scoped_lock lock(m_Mutex);
    isotp_poll(&m_Link);
}

std::optional<std::span<const std::uint8_t>> CanIsoTpEndpoint::OnCanFrame(
    const std::uint8_t* data, std::uint8_t size)
{
    std::scoped_lock lock(m_Mutex);

    /* The library's signature is non-const; it does not write through it. */
    isotp_on_can_message(&m_Link, const_cast<std::uint8_t*>(data), size);

    std::uint16_t received = 0;
    if(isotp_receive(&m_Link, m_MessageBuf, sizeof(m_MessageBuf), &received) != ISOTP_RET_OK)
        return std::nullopt;

    DBG("iso-tp recv: %d", received);
    m_Received.emplace_back(reinterpret_cast<const char*>(m_MessageBuf), received);
    m_LastReceived = m_Clock.Now();

    return std::span<const std::uint8_t>{ m_MessageBuf, received };
}

std::vector<std::string> CanIsoTpEndpoint::TakeReceivedFrames()
{
    std::scoped_lock lock(m_Mutex);
    return std::exchange(m_Received, {});
}

void CanIsoTpEndpoint::ClearReceivedFrames()
{
    std::scoped_lock lock(m_Mutex);
    m_Received.clear();
}

std::uint32_t CanIsoTpEndpoint::MillisecondsSinceLastFrame() const
{
    std::scoped_lock lock(m_Mutex);
    const std::int64_t difference = std::chrono::duration_cast<std::chrono::milliseconds>(
        m_Clock.Now() - m_LastReceived).count();
    return static_cast<std::uint32_t>(
        std::clamp<std::int64_t>(difference, 0, std::numeric_limits<std::uint32_t>::max()));
}

/* The ISO-TP library's global hooks. They are not per-link and have nothing to
   do with CAN entry management, which is where they used to sit. */

extern "C" void isotp_user_debug(const char* message, ...)
{
    char buffer[256];
    va_list args;
    va_start(args, message);
    vsnprintf(buffer, sizeof(buffer), message, args);
    va_end(args);

    LOG(LogLevel::Verbose, "IsoTP: {}", buffer);
    DBG("%s", buffer);
}

extern "C" uint32_t isotp_user_get_ms(void)
{
    return utils::GetTickCount();
}

extern "C" int isotp_user_send_can(const uint32_t arbitration_id, const uint8_t* data, const uint8_t size)
{
    /* Deliberately refuses: every link this application creates is given a
       per-link send callback, so reaching the global one means a link was
       initialised without one. */
    (void)arbitration_id;
    (void)data;
    (void)size;
    return ISOTP_RET_ERROR;
}
