#pragma once

#include "SerialPortConnectionStatus.hpp"

// The minimal serial lifecycle needed by protocol/application code. Port and
// TCP configuration live in separate interfaces so consumers do not need the
// complete SerialPortBase surface.
class ISerialTransport
{
public:
    virtual ~ISerialTransport() = default;

    [[nodiscard]] virtual bool IsOpen() = 0;
    [[nodiscard]] virtual SerialPortConnectionState GetConnectionState() const = 0;
    virtual void Open() = 0;
    virtual void Close() = 0;
    [[nodiscard]] virtual bool IsOk() const = 0;
    [[nodiscard]] virtual bool IsErrorPresent() const = 0;
};
