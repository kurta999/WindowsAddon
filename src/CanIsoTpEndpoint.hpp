#pragma once

#include "CanModels.hpp"
#include "ICanTransport.hpp"
#include "IClock.hpp"

extern "C"
{
#include <isotp/isotp.h>
}

#include <chrono>
#include <cstdint>
#include <mutex>
#include <optional>
#include <span>
#include <string>
#include <vector>

// !\brief One ISO-TP link: segmented UDS transfers over a CAN bus.
//
// This was an IsoTpLink, three 4 KB buffers, two static C callbacks, a
// response identifier, a receive history and a timestamp, all inside
// CanEntryHandler - which is also a transmit scheduler, a receive cache, three
// XML loaders and a worker thread. The link is a protocol state machine and
// reads as one.
//
// Threading: everything touching the link takes m_Mutex. It did not used to.
// The worker thread polled the link under CanEntryHandler's model lock, the
// receive path fed frames in under the same lock, and SendIsoTpFrame - reached
// from DidHandler and from a std::async in the raw UDS dialog - took no lock
// at all, so a send could interleave with a poll on the same link state.
//
// The lock order that makes this safe is model lock -> this -> transport, and
// nothing acquires them the other way round: OnCanFrame hands a completed
// frame back to its caller as a span rather than calling out while holding
// m_Mutex, and CanSerialPort dispatches to its listener outside its own locks.
class CanIsoTpEndpoint
{
public:
    // !\brief `request_id` is the identifier requests go out on until
    // SetRequestId changes it.
    CanIsoTpEndpoint(ICanTransport& transport, IClock& clock, std::uint32_t request_id);

    /* isotp_set_callbacks stores `this` as the callback context, so this object
       cannot be copied or moved - the link would go on pointing at the address
       it was initialised at. CanEntryHandler holds one by value, where that
       held by accident rather than by statement. */
    CanIsoTpEndpoint(const CanIsoTpEndpoint&) = delete;
    CanIsoTpEndpoint& operator=(const CanIsoTpEndpoint&) = delete;
    CanIsoTpEndpoint(CanIsoTpEndpoint&&) = delete;
    CanIsoTpEndpoint& operator=(CanIsoTpEndpoint&&) = delete;

    // !\brief The identifier this end sends requests on.
    void SetRequestId(std::uint32_t frame_id);

    // !\brief The identifier this end expects answers on.
    void SetResponseId(std::uint32_t frame_id);
    [[nodiscard]] std::uint32_t ResponseId() const;

    // !\brief Begin a transfer. Consecutive frames are driven by Poll.
    void Send(std::uint32_t frame_id, const std::uint8_t* data, std::uint16_t size);

    // !\brief Let the link advance timers and send whatever it owes.
    void Poll();

    // !\brief Feed one bus frame in.
    // !\return The reassembled message, when this frame completed one.
    //
    // The span points into a buffer this object owns and stays valid only until
    // the next call: the caller is expected to hand it straight to its
    // listeners, which is what CanEntryHandler does.
    [[nodiscard]] std::optional<std::span<const std::uint8_t>> OnCanFrame(
        const std::uint8_t* data, std::uint8_t size);

    // !\brief Every message received since the last clear, and clears them.
    //
    // Returning by value rather than handing out a reference to the vector:
    // the raw UDS dialog used to hold one across a send and a wait while the
    // receive thread appended to it, which is a reallocation under an
    // iteration.
    [[nodiscard]] std::vector<std::string> TakeReceivedFrames();

    // !\brief Drop anything received so far.
    void ClearReceivedFrames();

    // !\brief How long ago the last message completed, in milliseconds.
    [[nodiscard]] std::uint32_t MillisecondsSinceLastFrame() const;

private:
    static int SendCan(void* context, std::uint32_t arbitration_id,
        const std::uint8_t* data, std::uint8_t size);
    static std::uint32_t Milliseconds(void* context);

    ICanTransport& m_Transport;
    IClock&        m_Clock;

    mutable std::mutex m_Mutex;

    IsoTpLink    m_Link{};
    std::uint8_t m_SendBuf[MAX_ISOTP_FRAME_LEN]{};
    std::uint8_t m_RecvBuf[MAX_ISOTP_FRAME_LEN]{};

    /* Where a completed message is reassembled before it is handed on. Was a
       bare 4096 next to two buffers spelled MAX_ISOTP_FRAME_LEN, which is 4096. */
    std::uint8_t m_MessageBuf[MAX_ISOTP_FRAME_LEN]{};

    std::uint32_t m_ResponseId = 0x7DA;

    std::vector<std::string>              m_Received;
    std::chrono::steady_clock::time_point m_LastReceived{};
};
