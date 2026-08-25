#pragma once

#include "../utils/ScalarTypeDispatch.hpp"

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
#include <variant>
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
    /* The declaration order is the row order of modbus_types::kTraits, which
       indexes straight into this enum. */
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

// !\brief How one Modbus row is displayed.
//
// These used to sit loose among the protocol fields of ModbusItem - offset,
// type, byte order - so the value object describing a register also described a
// grid cell. Naming the presentation half lets it be copied, compared and
// edited on its own; ModbusMap already does the same through TextStyle.
//
// The colours stay optional: "no colour configured" is a different thing from
// "configured black", and the grid renders the two differently.
struct ModbusItemPresentation
{
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

    // !\brief Decimal places shown for a floating point register
    uint8_t m_FloatPrecision = 3;

    // !\brief Up to two value-dependent colour rules
    std::array<ModbusConditionalColorRule, 2> m_ConditionalColors;

    // !\brief Two-point linear scaling applied before display
    ModbusValueScaling m_ValueScaling;
};

// !\brief The value a Modbus register currently holds.
//
// ModbusItem used to carry three parallel members for this - m_Value, m_fValue
// and m_dValue - of which exactly one was live and the other two kept whatever
// an earlier type had left behind. Every reader had to consult m_Type to learn
// which, and a decoder that filled the wrong one left the grid showing a number
// the device never sent. One variant makes the stale pair impossible: storing a
// float replaces whatever was held before it.
//
// Reading through an accessor that does not match the alternative currently
// held yields zero rather than throwing. That is what the three fields did
// while only one of them had ever been written, and it is what the display
// paths still expect right after ClearValues().
class ModbusRegisterValue
{
public:
    using Storage = std::variant<uint64_t, float, double>;

    constexpr ModbusRegisterValue() noexcept = default;
    constexpr explicit ModbusRegisterValue(uint64_t value) noexcept : m_storage(value) {}

    [[nodiscard]] constexpr uint64_t Integer() const noexcept
    {
        const auto* value = std::get_if<uint64_t>(&m_storage);
        return value ? *value : 0;
    }

    [[nodiscard]] constexpr float Float() const noexcept
    {
        const auto* value = std::get_if<float>(&m_storage);
        return value ? *value : 0.0f;
    }

    [[nodiscard]] constexpr double Double() const noexcept
    {
        const auto* value = std::get_if<double>(&m_storage);
        return value ? *value : 0.0;
    }

    // !\brief Stores a value and reports whether it differs from the old one.
    //
    // Every decoder wants both answers. Spelling the comparison out at each
    // call site is what allowed one of them to test m_fValue and then assign
    // m_Value.
    bool SetInteger(uint64_t value) noexcept { return Replace(Storage{ value }); }
    bool SetFloat(float value) noexcept { return Replace(Storage{ value }); }
    bool SetDouble(double value) noexcept { return Replace(Storage{ value }); }

    // !\brief Back to "nothing has been read from the device yet".
    void Reset() noexcept { m_storage = uint64_t{ 0 }; }

    // !\brief Hands the held alternative to fn, for type-agnostic formatting.
    template <typename F>
    decltype(auto) Visit(F&& fn) const { return std::visit(std::forward<F>(fn), m_storage); }

private:
    bool Replace(const Storage& next) noexcept
    {
        if(m_storage == next)
            return false;
        m_storage = next;
        return true;
    }

    Storage m_storage{ uint64_t{ 0 } };
};

class ModbusItem : public ModbusItemPresentation
{
public:
    ModbusItem(const std::string& name, uint8_t fav_level, size_t offset, ModbusBitfieldType type, ModbusValueFormat value_format, const std::string& desc, 
        ModbusMapping& map, int64_t min_val, int64_t max_val, uint64_t value, std::optional<uint32_t> color_ = {}, std::optional<uint32_t> bg_color_ = {}, std::optional<bool> is_bold_ = false,
        std::optional<float> scale = {}, std::optional<std::string> font_face = {},
        ModbusRegisterByteOrder byte_order = ModbusRegisterByteOrder::Default) :
        ModbusItemPresentation{ color_, bg_color_, is_bold_.value_or(false) },
        m_Name(name), m_FavLevel(fav_level), m_Type(type), m_Format(value_format), m_Offset(offset),
        m_Value(value), m_Mapping(std::move(map)), m_Desc(desc), m_Min(min_val), m_Max(max_val),
        m_NetworkByteOrder(byte_order)
    {
        if(scale.has_value())
            m_scale = *scale;
        if(font_face.has_value())
            m_font_face = *font_face;
    }

    ModbusItem(const std::string& name, uint8_t fav_level, size_t offset, ModbusBitfieldType type,
        ModbusValueFormat value_format, const std::string& desc, int64_t min_val, int64_t max_val,
        uint64_t value, std::optional<uint32_t> color_ = {}, std::optional<uint32_t> bg_color_ = {},
        std::optional<bool> is_bold_ = false, std::optional<float> scale = {},
        std::optional<std::string> font_face = {},
        ModbusRegisterByteOrder byte_order = ModbusRegisterByteOrder::Default) :
        ModbusItemPresentation{ color_, bg_color_, is_bold_.value_or(false) },
        m_Name(name), m_FavLevel(fav_level), m_Type(type), m_Format(value_format), m_Offset(offset),
        m_Value(value), m_Desc(desc), m_Min(min_val), m_Max(max_val),
        m_NetworkByteOrder(byte_order)
    {
        if(scale)
            m_scale = *scale;
        if(font_face)
            m_font_face = *font_face;
    }

    // Defined out of line in ModbusTypeTraits.hpp, which owns the table.
    static size_t GetTypeSize(ModbusBitfieldType type);

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

    // !\brief What the device last reported for this register.
    ModbusRegisterValue m_Value;

    ModbusMapping m_Mapping;


    std::string m_Desc;

    int64_t m_Min;
    
    int64_t m_Max;


    int m_ManualAddress = -1;

    ModbusRegisterByteOrder m_NetworkByteOrder = ModbusRegisterByteOrder::Default;


    uint32_t branches = 0;

};


using ModbusItemType = std::vector<std::unique_ptr<ModbusItem>>;

// Defined in ModbusTypeTraits.hpp; declared here because ModbusItem uses it.
bool IsModbusScalingSupported(ModbusBitfieldType type);

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
            return item.m_Value.Float();
        case ModbusBitfieldType::MBT_DOUBLE:
            return item.m_Value.Double();
        case ModbusBitfieldType::MBT_BOOL:
            return item.m_Value.Integer() != 0 ? 1.0 : 0.0;
        case ModbusBitfieldType::MBT_I16:
            return static_cast<double>(static_cast<int16_t>(item.m_Value.Integer() & 0xFFFF));
        case ModbusBitfieldType::MBT_I32:
            return static_cast<double>(static_cast<int32_t>(item.m_Value.Integer() & 0xFFFFFFFF));
        case ModbusBitfieldType::MBT_I64:
            return static_cast<double>(static_cast<int64_t>(item.m_Value.Integer()));
        default:
            return static_cast<double>(item.m_Value.Integer());
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

// !\brief The raw register value that displays as `display` under `scaling` -
// the inverse of the formula directly above, kept next to it so the two
// cannot drift. It lived hand-derived in the holding-register edit path
// of ModbusDataPanel, a file away from the formula it inverts.
inline double GetModbusRawFromDisplayValue(const ModbusValueScaling& scaling, double display)
{
    const double slope = (scaling.y2 - scaling.y1) / (scaling.x2 - scaling.x1);
    return ((display - scaling.y1) / slope) + scaling.x1;
}

// !\brief The numeric base a register's text parses and renders in.
inline int GetModbusNumericBase(ModbusValueFormat format)
{
    return format == MVF_HEX ? 16 : format == MVF_BIN ? 2 : 10;
}

// !\brief A layout that holds more than one device, and can switch between them.
//
// Only the multi-device JSON format has this; the XML format does not, which is
// why it is a separate role rather than three more methods on the loader.
class IModbusDeviceCatalog
{
public:
    virtual ~IModbusDeviceCatalog() = default;

    [[nodiscard]] virtual std::vector<std::string> GetAvailableDevices(const std::filesystem::path& path) const = 0;
    virtual bool SelectDevice(const std::string& device) = 0;
    [[nodiscard]] virtual std::string GetSelectedDevice() const = 0;
};

// !\brief One device's complete register layout: the four tables, their
// configured sizes and offsets, and the slave address they answer on.
//
// These six travelled together as out-parameters through Load and Save, so the
// interface below - and both of its implementations, the JSON persistence
// helpers and the test loader - each restated the same clump. Nothing in the
// signature said that Load clears all four tables before filling them, and a
// caller that wanted to load into a scratch copy and swap it in under a lock
// could not say so. A layout is one value: it can be built, moved and replaced.
//
// Move-only, because ModbusItemType owns its items through unique_ptr.
struct ModbusDeviceLayout
{
    uint8_t slaveId = 1;
    ModbusItemType coils;
    ModbusItemType inputStatus;
    ModbusItemType holding;
    ModbusItemType input;
    NumModbusEntries counts;
};

class IModbusEntryLoader
{
public:
    virtual ~IModbusEntryLoader() = default;

    // !\brief Reads `path`, or nothing when it cannot be read or parsed.
    //
    // !\param fallback_slave_id What the layout's slave address should be when
    //        the file does not name one. `slave_id` used to be an in/out
    //        parameter carrying exactly this, which is why it was so easy to
    //        miss: switching between devices in a JSON file that declares no
    //        default_slave_id must not reset the address the user configured.
    [[nodiscard]] virtual std::optional<ModbusDeviceLayout> Load(
        const std::filesystem::path& path, uint32_t branch, uint8_t fallback_slave_id) = 0;

    virtual bool Save(const std::filesystem::path& path, const ModbusDeviceLayout& layout) const = 0;

    // !\brief The device catalog behind this loader, when it has one.
    //
    // These three used to be methods on this interface with do-nothing
    // defaults, so XmlModbusEntryLoader "implemented" them by returning empty
    // results - it has no notion of devices at all. Asking for the capability
    // instead lets a caller find out whether it exists.
    [[nodiscard]] virtual IModbusDeviceCatalog* DeviceCatalog() { return nullptr; }
    [[nodiscard]] const IModbusDeviceCatalog* DeviceCatalog() const
    {
        return const_cast<IModbusEntryLoader*>(this)->DeviceCatalog();
    }
};

// !\brief The scalar a register value is decoded as. A bool travels as one
// byte; a string is not a scalar at all.
[[nodiscard]] constexpr utils::ScalarType ScalarTypeOf(ModbusBitfieldType type)
{
    switch(type)
    {
        case MBT_BOOL:
        case MBT_UI8:    return utils::ScalarType::U8;
        case MBT_I8:     return utils::ScalarType::I8;
        case MBT_UI16:   return utils::ScalarType::U16;
        case MBT_I16:    return utils::ScalarType::I16;
        case MBT_UI32:   return utils::ScalarType::U32;
        case MBT_I32:    return utils::ScalarType::I32;
        case MBT_UI64:   return utils::ScalarType::U64;
        case MBT_I64:    return utils::ScalarType::I64;
        case MBT_FLOAT:  return utils::ScalarType::Float;
        case MBT_DOUBLE: return utils::ScalarType::Double;
        case MBT_STRING:
        case MBT_INVALID: break;
    }
    return utils::ScalarType::None;
}

/* Type dispatch, over the same switch CAN uses - the two used to be separate
   copies of it, differing only in the enumerator prefix. */
template <typename F>
void DispatchModbusBitfieldType(ModbusBitfieldType type, F&& fn)
{
    utils::DispatchScalarType(ScalarTypeOf(type), std::forward<F>(fn));
}

// !\brief Told when displayed register values have changed.
//
// This and IModbusLogView used to be one interface called IModbusHelper. The
// polling handler only ever calls these two methods; AppendLog and
// OnMaxEntriesReached belong to the communication-log view, and requiring both
// halves from one implementer is what made "helper" the only name that fitted.
class IModbusValueObserver
{
public:
    enum class Table : uint8_t { Coils, InputStatus, Holding, Input };

    virtual ~IModbusValueObserver() = default;

    // !\brief Redraw everything. The fallback when nothing finer is known.
    virtual void RefreshItems() = 0;

    // !\brief Redraw only the rows of one table that actually changed.
    virtual void QueueValueChanges(Table, const std::vector<uint8_t>&) { RefreshItems(); }
};

// !\brief The communication-log view: recorded frames, and the moment the
// recording buffer fills up.
class IModbusLogView
{
public:
    virtual ~IModbusLogView() = default;

    virtual void AppendLog(std::chrono::steady_clock::time_point& t1, uint8_t direction, uint8_t fcode,
        uint8_t error, const std::vector<uint8_t>& data) = 0;
    virtual void OnMaxEntriesReached() = 0;
};
