#pragma once

#include <string>
#include <chrono>
#include <functional>

#include "IComPortConfig.hpp"
#include "ISerialTransport.hpp"
#include "ITcpEndpointConfig.hpp"

class CallbackAsyncSerial;

using SerialRecvFunction = std::function<void(const char*, size_t)>;
using SerialSendFunction = std::function<void(CallbackAsyncSerial&)>;

// Combines the common port lifecycle with the two focused config sub-interfaces.
// Code that only needs TCP configuration can depend on ITcpEndpointConfig alone,
// and code that only needs serial configuration can depend on IComPortConfig alone.
class ISerialPort : public ISerialTransport, public IComPortConfig, public ITcpEndpointConfig
{
public:
    virtual ~ISerialPort() = default;

    virtual void InitInternal(const std::string& serial_name, std::chrono::milliseconds main_timeout, std::chrono::milliseconds exception_timeout,
        SerialRecvFunction recv_function, SerialSendFunction send_function, uint32_t baudrate = 921600, bool auto_open = false) = 0;
    virtual void InitInternal(const std::string& ip, uint16_t port, bool auto_open, std::chrono::milliseconds main_timeout, std::chrono::milliseconds exception_timeout,
        SerialRecvFunction recv_function, SerialSendFunction send_function) = 0;
    virtual void DeInitInternal() = 0;
    virtual void SetEnabled(bool enable) = 0;
    virtual bool IsEnabled() const = 0;

};
