#pragma once

#include <filesystem>
#include <bitset>
#include <array>
#include <chrono>
#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "IBasicGuiCustomization.hpp"

enum class ModbusByteOrder
{
    LittleEndian,
    BigEndian
};

enum class ModbusRegisterByteOrder : uint8_t
{
    Default,
    BigEndian,
    LittleEndian,
    BigEndianByteSwap,
    LittleEndianByteSwap
};

class NumModbusEntries
{
public:
    size_t coils = 0xFFFF;
    size_t inputStatus = 0xFFFF;
    size_t inputRegisters = 0xFFFF;
    size_t holdingRegisters = 0xFFFF;

    uint16_t coilsOffset = 0;
    uint16_t inputStatusOffset = 0;
    uint16_t inputOffset = 0;
    uint16_t holdingOffset = 0;
};

enum ModbusBitfieldType : uint8_t
{
    MBT_BOOL, MBT_UI8, MBT_I8, MBT_UI16, MBT_I16, MBT_UI32, MBT_I32, MBT_UI64, MBT_I64, MBT_FLOAT, MBT_DOUBLE, MBT_STRING, MBT_INVALID
};

enum ModbusValueFormat : uint8_t
{
    MVF_DEC, MVF_HEX, MVF_BIN
};

enum class ModbusConditionalColorComparison : uint8_t
{
    NotUsed,
    EqualTo,
    GreaterThan,
    LessThan,
    GreaterThanOrEqualTo,
    LessThanOrEqualTo
};

struct ModbusConditionalColorRule
{
    ModbusConditionalColorComparison comparison = ModbusConditionalColorComparison::NotUsed;
    double value = 0.0;
    std::optional<uint32_t> color;
    std::optional<uint32_t> background_color;
};

struct ModbusValueScaling
{
    bool enabled = false;
    double x1 = 0.0;
    double y1 = 0.0;
    double x2 = 65535.0;
    double y2 = 6553.5;
    uint8_t precision = 2;
};

class ModbusMap : public TextStyle
{
public:
    ModbusMap(const std::string& name, ModbusBitfieldType type, uint8_t size, size_t min_val, size_t max_val, const std::string& description,
        uint32_t color, uint32_t bg_color, bool is_bold, float scale) :
        TextStyle(color, bg_color, is_bold, scale), m_Type(type), m_Name(name), m_Size(size),
        m_MinVal(min_val), m_MaxVal(max_val), m_Description(description)
    {

    }

    //std::map<uint8_t, std::variant<bool, uint8_t, int8_t, uint16_t, int16_t, uint32_t, int32_t, uint64_t, int64_t, float, double, std::string>> m_Type;

    // !\brief Mapping type
    ModbusBitfieldType m_Type;

    // !\brief Mapping name
    std::string m_Name;

    // !\brief Bit length (starting from it's offset)
    uint8_t m_Size;

    // !\brief Minimum value
    size_t m_MinVal;

    // !\brief Maximum value
    size_t m_MaxVal;

    // !\brief Description (or whatever, more info about bitfields)
    std::string m_Description;
};

using ModbusMapping = std::map<uint8_t, std::unique_ptr<ModbusMap>>;

class ModbusItem
{
public:
    ModbusItem(const std::string& name, uint8_t fav_level, size_t offset, ModbusBitfieldType type, ModbusValueFormat value_format, const std::string& desc, 
        ModbusMapping& map, int64_t min_val, int64_t max_val, uint64_t value, std::optional<uint32_t> color_ = {}, std::optional<uint32_t> bg_color_ = {}, std::optional<bool> is_bold_ = false,
        std::optional<float> scale = {}, std::optional<std::string> font_face = {},
        ModbusRegisterByteOrder byte_order = ModbusRegisterByteOrder::Default) :
        m_Name(name), m_FavLevel(fav_level), m_Type(type), m_Format(value_format), m_Offset(offset),
        m_Value(value), m_Mapping(std::move(map)), m_Desc(desc), m_Min(min_val), m_Max(max_val),
        m_color(color_), m_bg_color(bg_color_), m_is_bold(is_bold_), m_NetworkByteOrder(byte_order)
    {
        if(scale.has_value())
            m_scale = *scale;
        if(font_face.has_value())
            m_font_face = *font_face;
        if(is_bold_.has_value())
            m_is_bold = *is_bold_;
    }

    ModbusItem(const std::string& name, uint8_t fav_level, size_t offset, ModbusBitfieldType type,
        ModbusValueFormat value_format, const std::string& desc, int64_t min_val, int64_t max_val,
        uint64_t value, std::optional<uint32_t> color_ = {}, std::optional<uint32_t> bg_color_ = {},
        std::optional<bool> is_bold_ = false, std::optional<float> scale = {},
        std::optional<std::string> font_face = {},
        ModbusRegisterByteOrder byte_order = ModbusRegisterByteOrder::Default) :
        m_Name(name), m_FavLevel(fav_level), m_Type(type), m_Format(value_format), m_Offset(offset),
        m_Value(value), m_Desc(desc), m_Min(min_val), m_Max(max_val), m_color(color_),
        m_bg_color(bg_color_), m_is_bold(is_bold_.value_or(false)), m_NetworkByteOrder(byte_order)
    {
        if(scale)
            m_scale = *scale;
        if(font_face)
            m_font_face = *font_face;
    }

    static constexpr size_t GetTypeSize(ModbusBitfieldType type)
    {
        if(type == ModbusBitfieldType::MBT_UI32 || type == ModbusBitfieldType::MBT_I32 || type == ModbusBitfieldType::MBT_FLOAT)
            return 2;
        if(type == ModbusBitfieldType::MBT_UI64 || type == ModbusBitfieldType::MBT_I64 || type == ModbusBitfieldType::MBT_DOUBLE)
            return 4;
        return 1;
    }

    size_t GetSize() const
    {
        return m_RegisterSize.value_or(GetTypeSize(m_Type));
    }

    std::string m_Name;
    uint8_t m_FavLevel = 0;

    ModbusBitfieldType m_Type;
    ModbusValueFormat m_Format;

    size_t m_Offset;

    // A configuration may deliberately declare a larger read-only block than
    // the scalar type displayed for its first register.
    std::optional<size_t> m_RegisterSize;

    uint64_t m_Value;
    float m_fValue = 0.0f;
    double m_dValue = 0.0;
    uint8_t m_FloatPrecision = 3;

    ModbusMapping m_Mapping;


    std::string m_Desc;

    int64_t m_Min;
    
    int64_t m_Max;

    // !\brief Text color
    std::optional<uint32_t> m_color;

    // !\brief Text background color
    std::optional<uint32_t> m_bg_color;

    // !\brief Is text bold?
    bool m_is_bold{ false };

    // !\brief Text scale
    float m_scale{ 1.0f };

    // !\brief Font face
    std::string m_font_face;

    int m_ManualAddress = -1;

    ModbusRegisterByteOrder m_NetworkByteOrder = ModbusRegisterByteOrder::Default;

    std::array<ModbusConditionalColorRule, 2> m_ConditionalColors;

    ModbusValueScaling m_ValueScaling;

    uint32_t branches = 0;

};


using ModbusItemType = std::vector<std::unique_ptr<ModbusItem>>;

inline bool IsModbusScalingSupported(ModbusBitfieldType type)
{
    return type == ModbusBitfieldType::MBT_UI16 || type == ModbusBitfieldType::MBT_I16 ||
        type == ModbusBitfieldType::MBT_UI32 || type == ModbusBitfieldType::MBT_I32;
}

inline bool IsModbusScalingActive(const ModbusItem& item)
{
    return item.m_ValueScaling.enabled && IsModbusScalingSupported(item.m_Type) &&
        item.m_Format == ModbusValueFormat::MVF_DEC && item.m_ValueScaling.x1 != item.m_ValueScaling.x2;
}

inline double GetModbusItemRawNumericValue(const ModbusItem& item)
{
    switch(item.m_Type)
    {
        case ModbusBitfieldType::MBT_FLOAT:
            return item.m_fValue;
        case ModbusBitfieldType::MBT_DOUBLE:
            return item.m_dValue;
        case ModbusBitfieldType::MBT_BOOL:
            return item.m_Value != 0 ? 1.0 : 0.0;
        case ModbusBitfieldType::MBT_I16:
            return static_cast<double>(static_cast<int16_t>(item.m_Value & 0xFFFF));
        case ModbusBitfieldType::MBT_I32:
            return static_cast<double>(static_cast<int32_t>(item.m_Value & 0xFFFFFFFF));
        case ModbusBitfieldType::MBT_I64:
            return static_cast<double>(static_cast<int64_t>(item.m_Value));
        default:
            return static_cast<double>(item.m_Value);
    }
}

inline double GetModbusItemDisplayNumericValue(const ModbusItem& item)
{
    const double raw_value = GetModbusItemRawNumericValue(item);
    if(!IsModbusScalingActive(item))
        return raw_value;

    const ModbusValueScaling& scaling = item.m_ValueScaling;
    const double slope = (scaling.y2 - scaling.y1) / (scaling.x2 - scaling.x1);
    return slope * (raw_value - scaling.x1) + scaling.y1;
}

class IModbusEntryLoader
{
public:
    virtual ~IModbusEntryLoader() = default;

    virtual bool Load(const std::filesystem::path& path, uint8_t& slave_id, ModbusItemType& coils, ModbusItemType& input_status,
        ModbusItemType& holding, ModbusItemType& input, NumModbusEntries& num_entries, uint32_t branch) = 0;
    virtual bool Save(const std::filesystem::path& path, uint8_t& slave_id, ModbusItemType& coils, ModbusItemType& input_status,
        ModbusItemType& holding, ModbusItemType& input, NumModbusEntries& num_entries) const = 0;

    virtual std::vector<std::string> GetAvailableDevices(const std::filesystem::path&) const { return {}; }
    virtual bool SelectDevice(const std::string&) { return false; }
    virtual std::string GetSelectedDevice() const { return {}; }
};

/* Single-switch type-dispatch for Modbus bitfield types, mirrors DispatchBitfieldType in CanModels.hpp.
   Caller passes a generic lambda: [&]<typename T>() { ... } */
template <typename F>
void DispatchModbusBitfieldType(ModbusBitfieldType type, F&& fn)
{
    switch (type)
    {
        case MBT_BOOL:
        case MBT_UI8:    std::forward<F>(fn).template operator()<uint8_t>();  break;
        case MBT_I8:     std::forward<F>(fn).template operator()<int8_t>();   break;
        case MBT_UI16:   std::forward<F>(fn).template operator()<uint16_t>(); break;
        case MBT_I16:    std::forward<F>(fn).template operator()<int16_t>();  break;
        case MBT_UI32:   std::forward<F>(fn).template operator()<uint32_t>(); break;
        case MBT_I32:    std::forward<F>(fn).template operator()<int32_t>();  break;
        case MBT_UI64:   std::forward<F>(fn).template operator()<uint64_t>(); break;
        case MBT_I64:    std::forward<F>(fn).template operator()<int64_t>();  break;
        case MBT_FLOAT:  std::forward<F>(fn).template operator()<float>();    break;
        case MBT_DOUBLE: std::forward<F>(fn).template operator()<double>();   break;
        default: break;
    }
}

class IModbusHelper
{
public:
    enum class Table : uint8_t { Coils, InputStatus, Holding, Input };

    virtual ~IModbusHelper() = default;

    virtual void AppendLog(std::chrono::steady_clock::time_point& t1, uint8_t direction, uint8_t fcode, uint8_t error, const std::vector<uint8_t>& data) = 0;
    virtual void OnMaxEntriesReached() = 0;
    virtual void RefreshItems() = 0;
    virtual void QueueValueChanges(Table, const std::vector<uint8_t>&) { RefreshItems(); }
};
