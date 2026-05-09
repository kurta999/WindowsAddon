#pragma once

#include <cstdint>
#include <string>

class ITcpEndpointConfig
{
public:
    virtual ~ITcpEndpointConfig() = default;

    virtual void               SetTcp(bool is_tcp)          = 0;
    virtual bool               IsTcp()              const   = 0;
    virtual void               SetTcpIp(const std::string& ip) = 0;
    virtual const std::string& GetTcpIp()           const   = 0;
    virtual void               SetTcpPort(uint16_t port)    = 0;
    virtual uint16_t           GetTcpPort()         const   = 0;
};
