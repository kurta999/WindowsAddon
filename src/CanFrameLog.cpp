#include "pch_core.hpp"
#include "CanFrameLog.hpp"
#include "Utils.hpp"

#include <format>

std::string CanFrameLog::FormatLine(const CanLogEntry& entry, std::string_view comment,
    LineStyle style) const
{
    std::string hex;
    utils::ConvertHexBufferToString(entry.data, hex);

    const uint64_t elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        entry.last_execution - m_Start).count();
    const double seconds = static_cast<double>(elapsed) / 1000.0;
    const char* direction = entry.direction == 0 ? "TX" : "RX";
    const auto frame_id = static_cast<uint32_t>(entry.frame_id);

    if(style == LineStyle::Csv)
    {
        std::string line = std::format("{:.3f},{},{:X},{},{}",
            seconds, direction, frame_id, entry.data.size(), hex);
        if(!comment.empty())
            line += "," + std::string(comment);
        return line;
    }

    std::string line = std::format("{:<6.03f}{:<6}{:<6X}{:<6}{:<6}",
        seconds, direction, frame_id, entry.data.size(), hex);
    if(!comment.empty())
        line += std::format("   {:^6}", comment);
    return line;
}
