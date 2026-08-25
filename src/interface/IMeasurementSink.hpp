#pragma once

#include <cstddef>

// !\brief Where a decoded sensor broadcast goes.
//
// CustomMacro forwarded serial measurements with `Sensors::Get()->...`, one of
// the reverse dependencies that kept the macro engine tied to the rest of the
// application. The sink is handed in instead.
class IMeasurementSink
{
public:
    virtual ~IMeasurementSink() = default;

    // !\brief Handle one broadcast. `source` names where it arrived from.
    virtual void HandleIncomingMeasurements(const char* data, std::size_t len, const char* source) = 0;
};
