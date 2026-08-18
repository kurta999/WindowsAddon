#pragma once

#include <cstdint>
#include <span>

class ICanTransportListener
{
public:
    virtual ~ICanTransportListener() = default;

    virtual void OnFrameSent(std::uint32_t frame_id, std::uint8_t data_len, std::uint8_t* data) = 0;
    virtual void OnFrameReceived(std::uint32_t frame_id, std::uint8_t data_len, std::uint8_t* data) = 0;
};

class ICanTransport
{
public:
    virtual ~ICanTransport() = default;

    virtual void SetListener(ICanTransportListener* listener) noexcept = 0;
    virtual void Send(std::uint32_t frame_id, std::span<const std::uint8_t> data) = 0;
};
