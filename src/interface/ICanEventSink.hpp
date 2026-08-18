#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>

class ICanEventSink
{
public:
    virtual ~ICanEventSink() = default;

    virtual void OnCanFrameTransmitted(std::uint32_t frame_id, std::size_t count) = 0;
    virtual void OnCanRecordingSaved(const std::filesystem::path& path, std::int64_t duration_ns) = 0;
};
