#pragma once

#include <cstdint>
#include <filesystem>

class IModbusEventSink
{
public:
    virtual ~IModbusEventSink() = default;
    virtual void OnModbusRecordingSaved(const std::filesystem::path& path, std::int64_t duration_ns) = 0;
};
