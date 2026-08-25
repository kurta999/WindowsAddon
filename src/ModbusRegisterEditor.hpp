#pragma once

#include "interface/IModbusEntry.hpp"

#include <algorithm>
#include <memory>
#include <string>
#include "ModbusTypeTraits.hpp"

enum class ModbusRegisterEditAction { MoveUp, MoveDown, Delete, InsertAfter };

struct ModbusRegisterEditResult
{
    bool success = false;
    std::string error;
    size_t selected_index = 0;
};

inline size_t ModbusRegisterEnd(const ModbusItem& item)
{
    return item.m_Offset + item.GetSize();
}

inline size_t GetModbusRegisterCount(const ModbusItemType& items)
{
    size_t count = 0;
    for(const auto& item : items)
        if(item)
            count = std::max(count, ModbusRegisterEnd(*item));
    return count;
}

inline std::unique_ptr<ModbusItem> CreateDefaultInsertedModbusRegister(const ModbusItem& previous)
{
    auto item = std::make_unique<ModbusItem>("new_register", previous.m_FavLevel,
        ModbusRegisterEnd(previous), MBT_UI16, previous.m_Format, "", 0, 0, 0);
    item->branches = previous.branches;
    return item;
}

inline ModbusRegisterEditResult FindContiguousModbusGroup(const ModbusItemType& items, size_t index,
    size_t& group_start, size_t& group_end)
{
    if(index >= items.size() || !items[index])
        return { false, "No register is selected.", index };
    group_start = index;
    while(group_start > 0)
    {
        const size_t previous_end = ModbusRegisterEnd(*items[group_start - 1]);
        const size_t current_start = items[group_start]->m_Offset;
        if(previous_end < current_start)
            break;
        if(previous_end > current_start)
            return { false, "Register addresses overlap before the selected row.", index };
        --group_start;
    }
    group_end = index;
    while(group_end + 1 < items.size())
    {
        const size_t current_end = ModbusRegisterEnd(*items[group_end]);
        const size_t next_start = items[group_end + 1]->m_Offset;
        if(current_end < next_start)
            break;
        if(current_end > next_start)
            return { false, "Register addresses overlap after the selected row.", index };
        ++group_end;
    }
    return { true, {}, index };
}

inline void ReflowModbusRegisterGroup(ModbusItemType& items, size_t group_start, size_t group_end,
    size_t start_offset)
{
    size_t offset = start_offset;
    for(size_t i = group_start; i <= group_end; ++i)
    {
        items[i]->m_Offset = offset;
        offset += items[i]->GetSize();
    }
}

inline bool WouldOverlapNextModbusGroup(const ModbusItemType& items, size_t group_end)
{
    return group_end + 1 < items.size() &&
        ModbusRegisterEnd(*items[group_end]) >= items[group_end + 1]->m_Offset;
}

inline ModbusRegisterEditResult EditModbusRegisterLayout(ModbusItemType& items, size_t index,
    ModbusRegisterEditAction action)
{
    size_t group_start = 0;
    size_t group_end = 0;
    auto group = FindContiguousModbusGroup(items, index, group_start, group_end);
    if(!group.success)
        return group;
    const size_t group_offset = items[group_start]->m_Offset;

    switch(action)
    {
        case ModbusRegisterEditAction::MoveUp:
            if(index == group_start)
                return { false, "The selected register is already at the top of this address group.", index };
            std::swap(items[index - 1], items[index]);
            ReflowModbusRegisterGroup(items, group_start, group_end, group_offset);
            return { true, {}, index - 1 };
        case ModbusRegisterEditAction::MoveDown:
            if(index == group_end)
                return { false, "The selected register is already at the bottom of this address group.", index };
            std::swap(items[index], items[index + 1]);
            ReflowModbusRegisterGroup(items, group_start, group_end, group_offset);
            return { true, {}, index + 1 };
        case ModbusRegisterEditAction::Delete:
            items.erase(items.begin() + index);
            if(group_start < group_end && group_start < items.size())
                ReflowModbusRegisterGroup(items, group_start, group_end - 1, group_offset);
            return { true, {}, std::min(index, items.empty() ? size_t{0} : items.size() - 1) };
        case ModbusRegisterEditAction::InsertAfter:
        {
            const size_t insert_index = index + 1;
            items.insert(items.begin() + insert_index, CreateDefaultInsertedModbusRegister(*items[index]));
            ++group_end;
            ReflowModbusRegisterGroup(items, group_start, group_end, group_offset);
            if(WouldOverlapNextModbusGroup(items, group_end))
            {
                items.erase(items.begin() + insert_index);
                --group_end;
                ReflowModbusRegisterGroup(items, group_start, group_end, group_offset);
                return { false, "Inserting here would overlap the next address group.", index };
            }
            return { true, {}, insert_index };
        }
    }
    return { false, "Unsupported register edit operation.", index };
}

inline bool IsSizedModbusRegisterType(ModbusBitfieldType type)
{
    return type == MBT_UI16 || type == MBT_I16 || type == MBT_UI32 || type == MBT_I32 ||
        type == MBT_UI64 || type == MBT_I64 || type == MBT_FLOAT || type == MBT_DOUBLE;
}

inline ModbusRegisterEditResult ChangeModbusRegisterType(ModbusItemType& items, size_t index,
    ModbusBitfieldType new_type)
{
    if(index >= items.size() || !items[index])
        return { false, "No register is selected.", index };
    if(items[index]->m_Type == MBT_BOOL)
        return { false, "Bit field types cannot be changed.", index };
    if(!IsSizedModbusRegisterType(new_type))
        return { false, "This register type is not supported for live Modbus register editing.", index };

    size_t group_start = 0;
    size_t group_end = 0;
    auto group = FindContiguousModbusGroup(items, index, group_start, group_end);
    if(!group.success)
        return group;
    const size_t group_offset = items[group_start]->m_Offset;
    const auto old_type = items[index]->m_Type;
    const auto old_size = items[index]->m_RegisterSize;
    const auto old_value = items[index]->m_Value;

    items[index]->m_Type = new_type;
    items[index]->m_RegisterSize.reset();
    items[index]->m_Value.Reset();
    ReflowModbusRegisterGroup(items, group_start, group_end, group_offset);
    if(WouldOverlapNextModbusGroup(items, group_end))
    {
        items[index]->m_Type = old_type;
        items[index]->m_RegisterSize = old_size;
        items[index]->m_Value = old_value;
        ReflowModbusRegisterGroup(items, group_start, group_end, group_offset);
        return { false, "Changing this register type would overlap the next address group.", index };
    }
    return { true, {}, index };
}
