#pragma once

#include "ITcpMessageExecutor.hpp"
#include "TcpMessageParser.hpp"

class TcpMessageExecutor : public ITcpMessageExecutor
{
public:
    TcpMessageExecutor();
    virtual ~TcpMessageExecutor() = default;

    TcpMessageReturn Process(const SharedSession& session, std::span<char> message) override;

private:
    TcpMessageReturn HandleAirQualityData(const SharedSession& session,
                                          std::span<const char> message);
    TcpMessageReturn HandleOpenExplorer(std::string_view path);
    TcpMessageReturn HandleGraphs(std::string_view file_on_disk);
};
