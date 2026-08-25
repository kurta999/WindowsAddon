#pragma once

#include "interface/IModbusEntry.hpp"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <iomanip>
#include <map>
#include <optional>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include <boost/algorithm/string/replace.hpp>
#include <boost/property_tree/json_parser.hpp>
#include <boost/property_tree/ptree.hpp>
#include "ModbusTypeTraits.hpp"
#include "Utils.hpp"
#include "utils/ColorCode.hpp"
#include "utils/EnumNameTable.hpp"

namespace modbus_json
{
/* Four hundred lines of `inline` compiled into every translation unit that
   wanted any of it, which is the two Modbus loaders and two test files. The
   bodies live in ModbusJsonPersistence.cpp now; what stays here is the two
   constexpr name tables and the handful of conversions whose body is one
   return, where inlining is the point rather than an accident. */
inline std::string NormalizeName(std::string name)
{
    name.erase(std::remove_if(name.begin(), name.end(), [](unsigned char c)
        { return std::isspace(c) || c == '-' || c == '_'; }), name.end());
    std::ranges::transform(name, name.begin(), [](unsigned char c)
        { return static_cast<char>(std::tolower(c)); });
    return name;
}

inline bool IsSettingsNode(const std::string& name)
{
    return NormalizeName(name) == "settings";
}

std::string DeviceNameFromBranchName(const std::string& branch_name);

std::string DeviceDisplayName(const std::string& json_device_name);

inline bool IsDeviceNode(const boost::property_tree::ptree& node)
{
    return node.get_child_optional("coils") || node.get_child_optional("input status") ||
        node.get_child_optional("holding registers") || node.get_child_optional("input registers");
}

std::vector<std::string> LoadJsonDeviceNames(const std::filesystem::path& path);

std::vector<std::string> LoadDeviceNames(const std::filesystem::path& path);

std::string ResolveDeviceName(const std::vector<std::string>& devices, const std::string& requested);

/* Type names come from modbus_types::kTraits, the table the rest of the Modbus
   code already reads. docs/architecture.md records that the MBT_* set was
   enumerated in seven separate places and consolidated onto that table; these
   two maps were an eighth copy the sweep did not reach, and they had to agree
   with it for a device file to round-trip. */
inline ModbusBitfieldType StringToModbusBitfieldType(const std::string& type)
{
    return modbus_types::FromName(type);
}

inline std::string ModbusBitfieldTypeToString(ModbusBitfieldType type)
{
    return std::string(modbus_types::NameOf(type));
}

/* Both of these were a std::map for reading and a switch for writing, and both
   had a value that appeared in only one of the halves: "default" was readable
   and NotUsed was writable. One table each now, so a name and its enumerator
   cannot come apart. */
inline constexpr utils::EnumNameTable kRegisterByteOrderNames{
    ModbusRegisterByteOrder::Default,
    std::array<utils::EnumName<ModbusRegisterByteOrder>, 5>{{
        { ModbusRegisterByteOrder::Default, "default" },
        { ModbusRegisterByteOrder::BigEndian, "big_endian" },
        { ModbusRegisterByteOrder::LittleEndian, "little_endian" },
        { ModbusRegisterByteOrder::BigEndianByteSwap, "big_endian_byte_swap" },
        { ModbusRegisterByteOrder::LittleEndianByteSwap, "little_endian_byte_swap" },
    }}
};

inline constexpr utils::EnumNameTable kComparisonNames{
    ModbusConditionalColorComparison::NotUsed,
    std::array<utils::EnumName<ModbusConditionalColorComparison>, 6>{{
        { ModbusConditionalColorComparison::NotUsed, "not_used" },
        { ModbusConditionalColorComparison::EqualTo, "equal_to" },
        { ModbusConditionalColorComparison::GreaterThan, "greater_than" },
        { ModbusConditionalColorComparison::LessThan, "less_than" },
        { ModbusConditionalColorComparison::GreaterThanOrEqualTo, "greater_than_or_equal_to" },
        { ModbusConditionalColorComparison::LessThanOrEqualTo, "less_than_or_equal_to" },
    }}
};

inline ModbusRegisterByteOrder StringToRegisterByteOrder(const std::string& order)
{
    return kRegisterByteOrderNames.FromName(order);
}

inline std::string RegisterByteOrderToString(ModbusRegisterByteOrder order)
{
    return std::string(kRegisterByteOrderNames.NameOf(order));
}

inline ModbusConditionalColorComparison StringToComparison(const std::string& comparison)
{
    return kComparisonNames.FromName(comparison);
}

inline std::string ComparisonToString(ModbusConditionalColorComparison comparison)
{
    return std::string(kComparisonNames.NameOf(comparison));
}

/* Colours go through utils/ColorCode.hpp. This file used to carry its own
   copy of the same seven-name table, and the two had drifted: 0xFF0000 was
   written back as "red" by the XML side and "0xFF0000" here, so one colour was
   spelled two ways depending on which format a device came from. Both spellings
   still load. An unreadable colour still reads as black here rather than being
   reported, because this header is used from targets without the logger. */

// !\brief One value, under whichever of two spellings the file uses.
//
// Three fields are written under a name this application no longer emits:
// description before desc, FavLevel before fav_level, LastVal before last_val.
// Each was read as a get() nested inside another get()'s default argument,
// which is the same idea spelled three times and does not say that the inner
// name is the older one.
template <typename T>
[[nodiscard]] T GetEither(const boost::property_tree::ptree& node,
    const char* key, const char* legacy_key, T fallback)
{
    if(const auto current = node.get_optional<T>(key))
        return *current;
    return node.get<T>(legacy_key, std::move(fallback));
}

inline std::optional<uint32_t> GetColor(const boost::property_tree::ptree& node, const char* key)
{
    const auto value = node.get_optional<std::string>(key);
    return value ? std::optional<uint32_t>(utils::ParseColor(*value).value_or(0)) : std::nullopt;
}

ModbusValueScaling GetScaling(const boost::property_tree::ptree& node);

inline bool ShouldSaveScaling(const ModbusValueScaling& scaling)
{
    const ModbusValueScaling defaults;
    return scaling.enabled || scaling.x1 != defaults.x1 || scaling.y1 != defaults.y1 ||
        scaling.x2 != defaults.x2 || scaling.y2 != defaults.y2 || scaling.precision != defaults.precision;
}

size_t LoadRegisterArray(const boost::property_tree::ptree& device, const char* array_name,
    ModbusItemType& output, ModbusBitfieldType default_type, uint32_t branch, bool force_type = false);

bool LoadDeviceFile(const std::filesystem::path& path, uint8_t& slave_id,
    ModbusItemType& coils, ModbusItemType& input_status, ModbusItemType& holding,
    ModbusItemType& input, NumModbusEntries& counts, uint32_t branch, const std::string& requested_device,
    std::string* loaded_device = nullptr);

void AppendRegisterArray(boost::property_tree::ptree& device, const char* name,
    const ModbusItemType& items);

std::filesystem::path NextBackupPath(const std::filesystem::path& path);

bool SaveDeviceFile(const std::filesystem::path& path, uint8_t slave_id,
    const ModbusItemType& coils, const ModbusItemType& input_status, const ModbusItemType& holding,
    const ModbusItemType& input, const NumModbusEntries& counts, const std::string& device_name,
    bool backup_existing);

// Compatibility overload for layouts that do not configure table base
// addresses explicitly, including existing PGL_DebugApp callers.
bool SaveDeviceFile(const std::filesystem::path& path, uint8_t slave_id,
    const ModbusItemType& coils, const ModbusItemType& input_status, const ModbusItemType& holding,
    const ModbusItemType& input, const std::string& device_name, bool backup_existing);

// !\brief The layout-shaped entry points, which is what IModbusEntryLoader
// speaks. The out-parameter forms above stay for callers outside this
// repository that already use them.
[[nodiscard]] std::optional<ModbusDeviceLayout> LoadLayout(const std::filesystem::path& path,
    uint32_t branch, uint8_t fallback_slave_id, const std::string& requested_device,
    std::string* loaded_device = nullptr);

bool SaveLayout(const std::filesystem::path& path, const ModbusDeviceLayout& layout,
    const std::string& device_name, bool backup_existing);
}
