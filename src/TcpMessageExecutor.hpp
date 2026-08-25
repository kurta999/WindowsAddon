#pragma once

#include "ITcpMessageExecutor.hpp"
#include "TcpMessageParser.hpp"

#include <functional>

class IMeasurementSink;

class TcpMessageExecutor : public ITcpMessageExecutor
{
public:
    // !\brief Where a sensor broadcast arriving over TCP is handed on.
    //
    // IMeasurementSink was added to take a Sensors::Get() out of the macro
    // engine, and Sensors has implemented it since. This was the last caller
    // still going to the singleton for the same operation.
    // !\brief `shared_drive_letter` is asked per request rather than held, so
    // a settings reload reaches a connection that is already open - which is
    // what reading it from the singleton used to give for free.
    TcpMessageExecutor(IMeasurementSink& measurements, std::function<char()> shared_drive_letter);
    virtual ~TcpMessageExecutor() = default;

    TcpMessageReturn Process(const SharedSession& session, std::span<char> message) override;

private:
    IMeasurementSink& m_Measurements;
    std::function<char()> m_SharedDriveLetter;

    TcpMessageReturn HandleAirQualityData(const SharedSession& session,
                                          std::span<const char> message);
    TcpMessageReturn HandleOpenExplorer(std::string_view path);
    TcpMessageReturn HandleGraphs(std::string_view file_on_disk);
};
