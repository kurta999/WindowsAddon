#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <span>

#include "CanTransportModels.hpp"

using CanFrameReceiver = std::function<void(std::uint32_t, std::uint8_t, std::uint8_t*)>;

class ICanDevice
{
public:
    virtual ~ICanDevice() = default;

    // !\brief Decode a chunk of received bytes into frames.
    //
    // The transport owns the receive buffer and the lock that guards it; this
    // used to take the transport's std::mutex&, which made every wire-protocol
    // strategy depend on how its caller happens to synchronise.
    virtual void DecodeReceivedBytes(std::span<const std::uint8_t> received, const CanFrameReceiver& receiver) = 0;

    // !\brief Send pending CAN frames from message queue
    // !\param serial_port [in] Reference to async serial port
    virtual size_t PrepareSendDataFormat(const std::shared_ptr<CanData>& data_ptr, char* out, size_t max_size, bool& remove_from_queue) = 0;
};
