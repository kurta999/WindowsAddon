#pragma once

#include "interface/IModbusEntry.hpp"

#include <cmath>

inline bool DoesModbusConditionalColorRuleMatch(const ModbusConditionalColorRule& rule, double value)
{
    switch(rule.comparison)
    {
        case ModbusConditionalColorComparison::EqualTo: return std::fabs(value - rule.value) < 0.000001;
        case ModbusConditionalColorComparison::GreaterThan: return value > rule.value;
        case ModbusConditionalColorComparison::LessThan: return value < rule.value;
        case ModbusConditionalColorComparison::GreaterThanOrEqualTo: return value >= rule.value;
        case ModbusConditionalColorComparison::LessThanOrEqualTo: return value <= rule.value;
        case ModbusConditionalColorComparison::NotUsed: return false;
    }
    return false;
}

inline const ModbusConditionalColorRule* FindMatchingModbusConditionalColorRule(const ModbusItem& item)
{
    const double value = GetModbusItemDisplayNumericValue(item);
    for(const auto& rule : item.m_ConditionalColors)
        if(rule.comparison != ModbusConditionalColorComparison::NotUsed &&
            DoesModbusConditionalColorRuleMatch(rule, value))
            return &rule;
    return nullptr;
}
