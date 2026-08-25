#include "pch_core.hpp"
#include "ModbusFrameLog.hpp"
#include "Logger.hpp"
#include "Utils.hpp"

#include <fstream>

void ModbusFrameLog::Append(uint8_t direction, uint8_t fcode, ModbusErrorType error, const uint8_t* data, size_t len)
{
    const auto now = std::chrono::steady_clock::now();
    std::scoped_lock lock(m_Mutex);
    m_Frames.emplace_back(std::make_unique<ModbusLogEntry>(direction, fcode, error, data, len, now));
}

void ModbusFrameLog::AppendEvent(std::vector<uint16_t>& data)
{
    const auto now = std::chrono::steady_clock::now();
    std::scoped_lock lock(m_Mutex);
    m_Events.emplace_back(std::make_unique<EventLogEntry>(data, now));
}

void ModbusFrameLog::TrimIfFull()
{
    std::scoped_lock lock(m_Mutex);
    if(m_Frames.size() >= m_MaxEntries.load(std::memory_order_relaxed))
    {
        m_Frames.clear();
        ++m_FramesGeneration;
    }
}

void ModbusFrameLog::Clear()
{
    std::scoped_lock lock(m_Mutex);
    m_Frames.clear();
    ++m_FramesGeneration;
}

namespace
{
    // !\brief Copy out whatever a cursor has not consumed yet, then move it on.
    template<class TEntry>
    ModbusLogSlice<TEntry> TakeSince(const std::vector<std::unique_ptr<TEntry>>& source,
        uint64_t generation, ModbusLogCursor& cursor)
    {
        ModbusLogSlice<TEntry> slice;
        if(cursor.generation != generation)
        {
            /* The fill the cursor belonged to is gone, so the view is showing
               entries that no longer exist and has to start over. */
            slice.restarted = true;
            cursor.index = 0;
            cursor.generation = generation;
        }

        for(size_t i = cursor.index; i < source.size(); ++i)
            slice.entries.push_back(*source[i]);

        cursor.index = source.size();
        return slice;
    }
}

ModbusLogSlice<ModbusLogEntry> ModbusFrameLog::TakeFramesSince(ModbusLogCursor& cursor) const
{
    std::scoped_lock lock(m_Mutex);
    return TakeSince(m_Frames, m_FramesGeneration, cursor);
}

ModbusLogSlice<EventLogEntry> ModbusFrameLog::TakeEventsSince(ModbusLogCursor& cursor) const
{
    std::scoped_lock lock(m_Mutex);
    return TakeSince(m_Events, m_EventsGeneration, cursor);
}

size_t ModbusFrameLog::FrameCount() const
{
    std::scoped_lock lock(m_Mutex);
    return m_Frames.size();
}

size_t ModbusFrameLog::EventCount() const
{
    std::scoped_lock lock(m_Mutex);
    return m_Events.size();
}

bool ModbusFrameLog::ExportFrames(const std::filesystem::path& path,
    std::chrono::steady_clock::time_point start_time) const
{
    std::scoped_lock lock(m_Mutex);
    if(m_Frames.empty())
        return false;

    std::ofstream out(path, std::ofstream::binary);
    if(!out.is_open())
    {
        LOG(LogLevel::Error, "Failed to open file for saving Modbus recording: {}", path.generic_string());
        return false;
    }

    out << "Time,Direction,FunctionCode,DataSize,Data\n";
    for(const auto& entry : m_Frames)
    {
        std::string hex;
        utils::ConvertHexBufferToString(entry->data, hex);
        const double elapsed =
            std::chrono::duration_cast<std::chrono::milliseconds>(entry->last_execution - start_time).count() / 1000.0;
        out << std::format("{:.3f},{},{},{},{}\n", elapsed,
            entry->direction == MODBUS_LOG_DIR_TX ? "TX" : "RX",
            static_cast<uint32_t>(entry->fcode), entry->data.size(), hex);
    }
    out.flush();
    return true;
}

bool ModbusFrameLog::ExportEvents(const std::filesystem::path& path,
    std::chrono::steady_clock::time_point start_time) const
{
    std::scoped_lock lock(m_Mutex);
    if(m_Events.empty())
        return false;

    std::ofstream out(path, std::ofstream::binary);
    if(!out.is_open())
    {
        LOG(LogLevel::Error, "Failed to open file for saving Modbus recording: {}", path.generic_string());
        return false;
    }

    out << "Time,DataSize,Data\n";
    for(const auto& entry : m_Events)
    {
        std::string hex;
        utils::ConvertHexBufferToString(entry->data, hex);
        const double elapsed =
            std::chrono::duration_cast<std::chrono::milliseconds>(entry->last_execution - start_time).count() / 1000.0;
        out << std::format("{:.3f},{}\n", elapsed, hex);
    }
    out.flush();
    return true;
}
