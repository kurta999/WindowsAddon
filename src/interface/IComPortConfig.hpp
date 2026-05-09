#pragma once

#include <cstdint>

class IComPortConfig
{
public:
    virtual ~IComPortConfig() = default;

    virtual void     SetComPort(uint16_t port)  = 0;
    virtual uint16_t GetComPort()         const = 0;
    virtual void     SetBaudrate(uint32_t baud) = 0;
    virtual uint32_t GetBaudrate()        const = 0;
};
