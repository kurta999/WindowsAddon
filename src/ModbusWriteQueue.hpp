#pragma once

#include <cstddef>
#include <cstdint>
#include <mutex>
#include <variant>
#include <vector>

// !\brief One pending register edit, by the kind of value it carries.
//
// The GUI queues edits from its own thread and the polling worker drains them.
// This used to be four parallel vectors drained by four near-identical loops,
// so adding a register type meant adding a fifth vector, a fifth drain branch
// and a fifth clear - and the drain order was fixed by type rather than by the
// order the user made the edits.
struct ModbusCoilWrite
{
    std::size_t id = 0;
    bool value = false;
};

struct ModbusHoldingWrite
{
    std::size_t id = 0;
    std::uint64_t value = 0;
};

struct ModbusFloatWrite
{
    std::size_t id = 0;
    float value = 0.0f;
};

struct ModbusDoubleWrite
{
    std::size_t id = 0;
    double value = 0.0;
};

// A closed set: a new register type is a new alternative, and every drain site
// stops compiling until it handles it.
using ModbusWrite = std::variant<ModbusCoilWrite, ModbusHoldingWrite, ModbusFloatWrite, ModbusDoubleWrite>;

// !\brief The edits waiting to go out on the next poll, in submission order.
class ModbusWriteQueue
{
public:
    void Push(ModbusWrite write)
    {
        std::scoped_lock lock(m_Mutex);
        m_Writes.push_back(write);
    }

    // !\brief Take everything queued so far, leaving the queue empty.
    [[nodiscard]] std::vector<ModbusWrite> Drain()
    {
        std::vector<ModbusWrite> drained;
        std::scoped_lock lock(m_Mutex);
        drained.swap(m_Writes);
        return drained;
    }

    // !\brief Discard queued edits. Used when the register model is replaced,
    // because the ids they carry no longer mean anything.
    void Clear()
    {
        std::scoped_lock lock(m_Mutex);
        m_Writes.clear();
    }

    [[nodiscard]] bool Empty() const
    {
        std::scoped_lock lock(m_Mutex);
        return m_Writes.empty();
    }

    [[nodiscard]] std::size_t Size() const
    {
        std::scoped_lock lock(m_Mutex);
        return m_Writes.size();
    }

private:
    mutable std::mutex m_Mutex;
    std::vector<ModbusWrite> m_Writes;
};
