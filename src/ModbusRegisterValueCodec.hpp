#pragma once

#include "interface/IModbusEntry.hpp"

#include <array>
#include <cstdint>
#include <cstring>
#include <vector>

inline uint16_t SwapModbusBytes16(uint16_t value)
{
    return static_cast<uint16_t>((value << 8) | (value >> 8));
}

inline ModbusRegisterByteOrder ResolveModbusRegisterByteOrder(ModbusBitfieldType type,
    ModbusRegisterByteOrder byte_order)
{
    if(byte_order != ModbusRegisterByteOrder::Default)
        return byte_order;
    if(type == MBT_UI32 || type == MBT_I32 || type == MBT_UI64 || type == MBT_I64 ||
        type == MBT_FLOAT || type == MBT_DOUBLE)
        return ModbusRegisterByteOrder::LittleEndian;
    return ModbusRegisterByteOrder::BigEndian;
}

inline uint16_t DecodeModbusRegister16(uint16_t word, ModbusRegisterByteOrder byte_order)
{
    return ResolveModbusRegisterByteOrder(MBT_UI16, byte_order) == ModbusRegisterByteOrder::LittleEndian
        ? SwapModbusBytes16(word) : word;
}

inline uint32_t DecodeModbusRegister32(uint16_t first, uint16_t second, ModbusRegisterByteOrder byte_order)
{
    switch(ResolveModbusRegisterByteOrder(MBT_UI32, byte_order))
    {
        case ModbusRegisterByteOrder::BigEndian: return (static_cast<uint32_t>(first) << 16) | second;
        case ModbusRegisterByteOrder::LittleEndian: return (static_cast<uint32_t>(second) << 16) | first;
        case ModbusRegisterByteOrder::BigEndianByteSwap:
            return (static_cast<uint32_t>(SwapModbusBytes16(first)) << 16) | SwapModbusBytes16(second);
        case ModbusRegisterByteOrder::LittleEndianByteSwap:
            return (static_cast<uint32_t>(SwapModbusBytes16(second)) << 16) | SwapModbusBytes16(first);
        case ModbusRegisterByteOrder::Default: break;
    }
    return (static_cast<uint32_t>(second) << 16) | first;
}

inline float DecodeModbusRegisterFloat(uint16_t first, uint16_t second, ModbusRegisterByteOrder byte_order)
{
    const uint32_t raw = DecodeModbusRegister32(first, second, byte_order);
    float value = 0.0f;
    std::memcpy(&value, &raw, sizeof(value));
    return value;
}

inline uint64_t DecodeModbusRegister64(uint16_t first, uint16_t second, uint16_t third,
    uint16_t fourth, ModbusRegisterByteOrder byte_order)
{
    switch(ResolveModbusRegisterByteOrder(MBT_DOUBLE, byte_order))
    {
        case ModbusRegisterByteOrder::BigEndian:
            return (static_cast<uint64_t>(first) << 48) | (static_cast<uint64_t>(second) << 32) |
                (static_cast<uint64_t>(third) << 16) | fourth;
        case ModbusRegisterByteOrder::LittleEndian:
            return (static_cast<uint64_t>(fourth) << 48) | (static_cast<uint64_t>(third) << 32) |
                (static_cast<uint64_t>(second) << 16) | first;
        case ModbusRegisterByteOrder::BigEndianByteSwap:
            return (static_cast<uint64_t>(SwapModbusBytes16(first)) << 48) |
                (static_cast<uint64_t>(SwapModbusBytes16(second)) << 32) |
                (static_cast<uint64_t>(SwapModbusBytes16(third)) << 16) | SwapModbusBytes16(fourth);
        case ModbusRegisterByteOrder::LittleEndianByteSwap:
            return (static_cast<uint64_t>(SwapModbusBytes16(fourth)) << 48) |
                (static_cast<uint64_t>(SwapModbusBytes16(third)) << 32) |
                (static_cast<uint64_t>(SwapModbusBytes16(second)) << 16) | SwapModbusBytes16(first);
        case ModbusRegisterByteOrder::Default: break;
    }
    return (static_cast<uint64_t>(fourth) << 48) | (static_cast<uint64_t>(third) << 32) |
        (static_cast<uint64_t>(second) << 16) | first;
}

inline double DecodeModbusRegisterDouble(uint16_t first, uint16_t second, uint16_t third,
    uint16_t fourth, ModbusRegisterByteOrder byte_order)
{
    const uint64_t raw = DecodeModbusRegister64(first, second, third, fourth, byte_order);
    double value = 0.0;
    std::memcpy(&value, &raw, sizeof(value));
    return value;
}

inline std::array<uint16_t, 2> EncodeModbusRegister32(uint32_t raw, ModbusRegisterByteOrder byte_order)
{
    const uint16_t high = static_cast<uint16_t>(raw >> 16);
    const uint16_t low = static_cast<uint16_t>(raw);
    switch(ResolveModbusRegisterByteOrder(MBT_UI32, byte_order))
    {
        case ModbusRegisterByteOrder::BigEndian: return { high, low };
        case ModbusRegisterByteOrder::LittleEndian: return { low, high };
        case ModbusRegisterByteOrder::BigEndianByteSwap: return { SwapModbusBytes16(high), SwapModbusBytes16(low) };
        case ModbusRegisterByteOrder::LittleEndianByteSwap: return { SwapModbusBytes16(low), SwapModbusBytes16(high) };
        case ModbusRegisterByteOrder::Default: break;
    }
    return { low, high };
}

inline std::array<uint16_t, 4> EncodeModbusRegister64(uint64_t raw, ModbusRegisterByteOrder byte_order)
{
    const uint16_t w3 = static_cast<uint16_t>(raw >> 48);
    const uint16_t w2 = static_cast<uint16_t>(raw >> 32);
    const uint16_t w1 = static_cast<uint16_t>(raw >> 16);
    const uint16_t w0 = static_cast<uint16_t>(raw);
    switch(ResolveModbusRegisterByteOrder(MBT_DOUBLE, byte_order))
    {
        case ModbusRegisterByteOrder::BigEndian: return { w3, w2, w1, w0 };
        case ModbusRegisterByteOrder::LittleEndian: return { w0, w1, w2, w3 };
        case ModbusRegisterByteOrder::BigEndianByteSwap:
            return { SwapModbusBytes16(w3), SwapModbusBytes16(w2), SwapModbusBytes16(w1), SwapModbusBytes16(w0) };
        case ModbusRegisterByteOrder::LittleEndianByteSwap:
            return { SwapModbusBytes16(w0), SwapModbusBytes16(w1), SwapModbusBytes16(w2), SwapModbusBytes16(w3) };
        case ModbusRegisterByteOrder::Default: break;
    }
    return { w0, w1, w2, w3 };
}

inline uint16_t EncodeModbusRegister16(uint16_t raw, ModbusRegisterByteOrder byte_order)
{
    return ResolveModbusRegisterByteOrder(MBT_UI16, byte_order) == ModbusRegisterByteOrder::LittleEndian
        ? SwapModbusBytes16(raw) : raw;
}

inline std::vector<uint16_t> EncodeModbusRegisterValue(const ModbusItem& item, uint64_t value)
{
    if(item.m_Type == MBT_UI32 || item.m_Type == MBT_I32)
    {
        const auto words = EncodeModbusRegister32(static_cast<uint32_t>(value), item.m_NetworkByteOrder);
        return { words.begin(), words.end() };
    }
    if(item.m_Type == MBT_UI64 || item.m_Type == MBT_I64)
    {
        const auto words = EncodeModbusRegister64(value, item.m_NetworkByteOrder);
        return { words.begin(), words.end() };
    }
    return { EncodeModbusRegister16(static_cast<uint16_t>(value), item.m_NetworkByteOrder) };
}

inline std::vector<uint16_t> EncodeModbusRegisterFloatValue(const ModbusItem& item, float value)
{
    uint32_t raw = 0;
    std::memcpy(&raw, &value, sizeof(raw));
    const auto words = EncodeModbusRegister32(raw, item.m_NetworkByteOrder);
    return { words.begin(), words.end() };
}

inline std::vector<uint16_t> EncodeModbusRegisterDoubleValue(const ModbusItem& item, double value)
{
    uint64_t raw = 0;
    std::memcpy(&raw, &value, sizeof(raw));
    const auto words = EncodeModbusRegister64(raw, item.m_NetworkByteOrder);
    return { words.begin(), words.end() };
}
