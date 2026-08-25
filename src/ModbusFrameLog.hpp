#pragma once

#include "interface/IModbusRecorder.hpp"

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <mutex>
#include <vector>

// !\brief One recorded Modbus frame.
class ModbusLogEntry
{
public:
    ModbusLogEntry(uint8_t dir, uint8_t fc, ModbusErrorType error, const uint8_t* data_, size_t data_len,
        std::chrono::steady_clock::time_point timepoint) :
        direction(dir), fcode(fc), error_type(error), last_execution(timepoint)
    {
        if(data_ && data_len)
            data.insert(data.end(), data_, data_ + data_len);
    }

    std::vector<uint8_t> data;
    uint8_t direction;
    uint8_t fcode;
    ModbusErrorType error_type;
    std::chrono::steady_clock::time_point last_execution;
};

// !\brief One recorded special-register (event log) read.
class EventLogEntry
{
public:
    EventLogEntry(std::vector<uint16_t>& data_, std::chrono::steady_clock::time_point timepoint) :
        last_execution(timepoint)
    {
        data = std::move(data_);
    }

    std::vector<uint16_t> data;
    std::chrono::steady_clock::time_point last_execution;
};

// !\brief How far a view has consumed a buffer, and which fill of it.
//
// TrimIfFull and Clear discard the whole buffer, so an index taken before one
// of those no longer refers to the same entries. The generation makes that
// detectable instead of leaving the view to infer it from a shrinking size.
struct ModbusLogCursor
{
    size_t index = 0;
    uint64_t generation = 0;
};

// !\brief Entries a view has not seen yet, copied out of the buffer.
//
// `restarted` means the buffer was discarded since the previous call and
// whatever the view is showing belongs to a fill that no longer exists.
template<class TEntry>
struct ModbusLogSlice
{
    std::vector<TEntry> entries;
    bool restarted = false;
};

// !\brief The frame recording buffer and its CSV export.
//
// This was five loose public members of ModbusEntryHandler - two vectors, a
// cap, a recording flag and a shared mutex - with the trimming and the export
// mixed into a class that also polls, writes registers and loads configuration.
// Nothing here needs a serial port, so it can be exercised on its own.
class ModbusFrameLog
{
public:
    // !\brief Record one frame.
    //
    // Stores whatever it is given: the transport checks IsRecording before
    // building the arguments, so this does not re-test the flag.
    void Append(uint8_t direction, uint8_t fcode, ModbusErrorType error, const uint8_t* data, size_t len);

    // !\brief Record one special-register read.
    void AppendEvent(std::vector<uint16_t>& data);

    // !\brief Drop everything once the buffer reaches its cap.
    //
    // The whole buffer is discarded rather than the oldest entries: the GUI
    // renders it by remembering how far it had already inserted, and dropping
    // from the front would silently shift every one of those positions.
    void TrimIfFull();

    void Clear();

    [[nodiscard]] bool IsRecording() const { return m_IsRecording.load(std::memory_order_relaxed); }
    void SetRecording(bool recording) { m_IsRecording.store(recording, std::memory_order_relaxed); }

    void SetMaxEntries(size_t max_entries) { m_MaxEntries.store(max_entries, std::memory_order_relaxed); }
    [[nodiscard]] size_t GetMaxEntries() const { return m_MaxEntries.load(std::memory_order_relaxed); }

    // !\brief Write the recorded frames as CSV. False when there is nothing to
    // write or the file could not be opened.
    [[nodiscard]] bool ExportFrames(const std::filesystem::path& path,
        std::chrono::steady_clock::time_point start_time) const;

    // !\brief Write the recorded special-register reads as CSV.
    [[nodiscard]] bool ExportEvents(const std::filesystem::path& path,
        std::chrono::steady_clock::time_point start_time) const;

    // !\brief Entries appended since `cursor`, which is advanced to match.
    //
    // The polling worker fills these buffers, so a view cannot hold iterators
    // into them: it takes the new entries as owning copies and renders them
    // after the lock is released. Copying costs one batch per refresh, which
    // is what arrived since the last one.
    [[nodiscard]] ModbusLogSlice<ModbusLogEntry> TakeFramesSince(ModbusLogCursor& cursor) const;
    [[nodiscard]] ModbusLogSlice<EventLogEntry> TakeEventsSince(ModbusLogCursor& cursor) const;

    [[nodiscard]] size_t FrameCount() const;
    [[nodiscard]] size_t EventCount() const;

private:
    // !\brief Bumped when the buffer it belongs to is discarded, see
    // ModbusLogCursor. One per buffer, so clearing the frames does not make
    // the event view believe its own entries went away.
    uint64_t m_FramesGeneration = 0;
    uint64_t m_EventsGeneration = 0;
    mutable std::mutex m_Mutex;
    std::vector<std::unique_ptr<ModbusLogEntry>> m_Frames;
    std::vector<std::unique_ptr<EventLogEntry>> m_Events;
    std::atomic<size_t> m_MaxEntries{ 30000 };
    std::atomic<bool> m_IsRecording{ false };
};
