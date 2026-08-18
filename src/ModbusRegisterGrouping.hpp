#pragma once

#include "interface/IModbusEntry.hpp"

#include <algorithm>
#include <limits>
#include <map>
#include <optional>
#include <utility>
#include <vector>

struct ModbusRegisterRange { size_t offset = 0; size_t count = 0; };

inline std::vector<ModbusRegisterRange> BuildContiguousModbusRegisterRanges(
    const ModbusItemType& items, uint32_t branch)
{
    std::vector<std::pair<size_t, size_t>> ranges;
    ranges.reserve(items.size());
    for(const auto& item : items)
    {
        if(!item || !(item->branches & branch) || item->GetSize() == 0)
            continue;
        ranges.push_back({ item->m_Offset, item->GetSize() });
    }
    if(ranges.empty())
        return {};
    std::sort(ranges.begin(), ranges.end());

    std::vector<ModbusRegisterRange> grouped;
    size_t start = ranges.front().first;
    size_t end = start + ranges.front().second;
    for(size_t i = 1; i < ranges.size(); ++i)
    {
        const size_t next_start = ranges[i].first;
        const size_t next_end = next_start + ranges[i].second;
        if(next_start <= end)
            end = std::max(end, next_end);
        else
        {
            grouped.push_back({ start, end - start });
            start = next_start;
            end = next_end;
        }
    }
    grouped.push_back({ start, end - start });
    return grouped;
}

struct GroupedModbusRegisterReadResult
{
    std::map<size_t, uint16_t> values;
    size_t read_count = 0;
    size_t failed_read_count = 0;
};

struct GroupedModbusBitReadResult
{
    std::map<size_t, uint8_t> values;
    size_t read_count = 0;
    size_t failed_read_count = 0;
};

template<typename ReadRegisters>
std::optional<GroupedModbusRegisterReadResult> ReadGroupedModbusRegisters(
    const ModbusItemType& items, uint32_t branch, ReadRegisters&& read_registers, uint16_t base_offset = 0)
{
    GroupedModbusRegisterReadResult result;
    for(const auto& range : BuildContiguousModbusRegisterRanges(items, branch))
    {
        if(range.offset > std::numeric_limits<uint16_t>::max() - base_offset ||
            range.count > std::numeric_limits<uint16_t>::max())
            return std::nullopt;
        auto registers = read_registers(static_cast<uint16_t>(base_offset + range.offset),
            static_cast<uint16_t>(range.count));
        ++result.read_count;
        if(!registers.has_value() || registers->size() < range.count)
        {
            ++result.failed_read_count;
            continue;
        }
        for(size_t i = 0; i < range.count; ++i)
            result.values[range.offset + i] = registers->at(i);
    }
    return result;
}

template<typename ReadBits>
std::optional<GroupedModbusBitReadResult> ReadGroupedModbusBits(
    const ModbusItemType& items, uint32_t branch, ReadBits&& read_bits, uint16_t base_offset = 0)
{
    GroupedModbusBitReadResult result;
    for(const auto& range : BuildContiguousModbusRegisterRanges(items, branch))
    {
        if(range.offset > std::numeric_limits<uint16_t>::max() - base_offset ||
            range.count > std::numeric_limits<uint16_t>::max())
        {
            ++result.failed_read_count;
            continue;
        }
        auto packed = read_bits(static_cast<uint16_t>(base_offset + range.offset),
            static_cast<uint16_t>(range.count));
        ++result.read_count;
        if(!packed.has_value() || packed->size() < (range.count + 7) / 8)
            return std::nullopt;
        for(size_t i = 0; i < range.count; ++i)
            result.values[range.offset + i] = static_cast<uint8_t>((packed->at(i / 8) >> (i % 8)) & 1);
    }
    return result;
}
