#pragma once

#include "CanModels.hpp"

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

// !\brief The recorded CAN frames, and how one is rendered as a line.
//
// This was five members and four methods of CanEntryHandler, which also owns
// the transmit list, the receive map, the ISO-TP link and a worker thread.
// Recording is the one part of it a caller can reason about on its own.
//
// Not internally synchronised, deliberately: CanEntryHandler's model lock
// guards this object exactly as it guarded the vector and the flag when they
// were its own members. The CAN log panel walks Entries() under that lock.
class CanFrameLog
{
public:
    // !\brief How a recorded frame is written out.
    //
    // The CSV export and the per-frame log view rendered the same five fields
    // from the same entry - elapsed seconds, direction, frame id, length,
    // payload - one with commas and one with column padding.
    enum class LineStyle : std::uint8_t { Csv, Columns };

    // !\brief The instant elapsed times in a log line are measured from.
    void SetStart(std::chrono::steady_clock::time_point start) { m_Start = start; }

    void Append(std::unique_ptr<CanLogEntry> entry) { m_Entries.push_back(std::move(entry)); }

    void Clear() { m_Entries.clear(); }

    [[nodiscard]] bool Empty() const { return m_Entries.empty(); }

    [[nodiscard]] const std::vector<std::unique_ptr<CanLogEntry>>& Entries() const { return m_Entries; }

    [[nodiscard]] bool IsRecording() const { return m_IsRecording; }
    void SetRecording(bool recording) { m_IsRecording = recording; }

    // !\brief One recorded frame as a line.
    // !\param comment Resolved by the caller: it is the only field of a log
    // line that needs the handler's transmit list and receive comment map.
    [[nodiscard]] std::string FormatLine(const CanLogEntry& entry, std::string_view comment,
        LineStyle style) const;

    // !\brief The header the CSV export writes above its rows.
    static constexpr std::string_view kCsvHeader = "Time,Direction,FrameID,DataSize,Data,Comment";

private:
    std::vector<std::unique_ptr<CanLogEntry>> m_Entries;
    std::chrono::steady_clock::time_point m_Start{};
    bool m_IsRecording = false;
};
