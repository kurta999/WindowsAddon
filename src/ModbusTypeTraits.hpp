#pragma once

#include "interface/IModbusEntry.hpp"

#include <array>
#include <algorithm>
#include <cstdint>
#include <string_view>

// !\brief Everything the rest of the code needs to know about a register type.
//
// The same set of MBT_* values was being enumerated separately in seven places:
// the XML name map, the register-count helper, the scaling predicate, the
// default byte order and three formatting switches. Adding a type meant finding
// all of them. It is one row here.
struct ModbusTypeTraits
{
    ModbusBitfieldType type = ModbusBitfieldType::MBT_INVALID;

    // !\brief The name used in Modbus.xml and the multi-device JSON.
    std::string_view name;

    // !\brief How many 16-bit registers the value occupies.
    std::uint8_t register_count = 1;

    // !\brief Whether the value is a signed integer.
    bool is_signed = false;

    // !\brief Whether the value is IEEE floating point.
    bool is_floating = false;

    // !\brief Whether a two-point linear scaling may be applied to it.
    bool supports_scaling = false;
};

namespace modbus_types
{
// Order matches the ModbusBitfieldType enum so lookup is a direct index.
inline constexpr std::array<ModbusTypeTraits, 13> kTraits{{
    { ModbusBitfieldType::MBT_BOOL,    "bool",     1, false, false, false },
    { ModbusBitfieldType::MBT_UI8,     "uint8_t",  1, false, false, false },
    { ModbusBitfieldType::MBT_I8,      "int8_t",   1, true,  false, false },
    { ModbusBitfieldType::MBT_UI16,    "uint16_t", 1, false, false, true  },
    { ModbusBitfieldType::MBT_I16,     "int16_t",  1, true,  false, true  },
    { ModbusBitfieldType::MBT_UI32,    "uint32_t", 2, false, false, true  },
    { ModbusBitfieldType::MBT_I32,     "int32_t",  2, true,  false, true  },
    { ModbusBitfieldType::MBT_UI64,    "uint64_t", 4, false, false, false },
    { ModbusBitfieldType::MBT_I64,     "int64_t",  4, true,  false, false },
    { ModbusBitfieldType::MBT_FLOAT,   "float",    2, true,  true,  false },
    { ModbusBitfieldType::MBT_DOUBLE,  "double",   4, true,  true,  false },
    { ModbusBitfieldType::MBT_STRING,  "string",   1, false, false, false },
    { ModbusBitfieldType::MBT_INVALID, "invalid",  1, false, false, false },
}};

// !\brief The row for a type, or the invalid row when it is out of range.
[[nodiscard]] constexpr const ModbusTypeTraits& Of(ModbusBitfieldType type)
{
    const auto index = static_cast<std::size_t>(type);
    return index < kTraits.size() ? kTraits[index] : kTraits.back();
}

// !\brief The type a configuration file names, or MBT_INVALID.
[[nodiscard]] inline ModbusBitfieldType FromName(std::string_view name)
{
    const auto it = std::find_if(kTraits.begin(), kTraits.end(),
        [name](const ModbusTypeTraits& traits) { return traits.name == name; });
    return it != kTraits.end() ? it->type : ModbusBitfieldType::MBT_INVALID;
}

// !\brief How a type is written back out to a configuration file.
[[nodiscard]] constexpr std::string_view NameOf(ModbusBitfieldType type)
{
    return Of(type).name;
}
}

// The two helpers IModbusEntry.hpp declares; they read the table above rather
// than re-enumerating the types.
inline size_t ModbusItem::GetTypeSize(ModbusBitfieldType type)
{
    return modbus_types::Of(type).register_count;
}

inline bool IsModbusScalingSupported(ModbusBitfieldType type)
{
    return modbus_types::Of(type).supports_scaling;
}
