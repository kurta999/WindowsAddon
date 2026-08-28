#pragma once

#include "utils/ScalarTypeDispatch.hpp"

#include <vector>
#include <chrono>
#include <string>
#include <optional>
#include <cstdint>

#include "interface/IBasicGuiCustomization.hpp"

constexpr uint8_t CAN_LOG_DIR_TX = 0;
constexpr uint8_t CAN_LOG_DIR_RX = 1;
constexpr size_t MAX_ISOTP_FRAME_LEN = 4096;

enum CanBitfieldType : uint8_t
{
    CBT_BOOL, CBT_UI8, CBT_I8, CBT_UI16, CBT_I16, CBT_UI32, CBT_I32, CBT_UI64, CBT_I64, CBT_FLOAT, CBT_DOUBLE, CBT_INVALID
};

// !\brief The scalar a CAN field is decoded as. A bool travels as one byte.
[[nodiscard]] constexpr utils::ScalarType ScalarTypeOf(CanBitfieldType type)
{
    switch(type)
    {
        case CBT_BOOL:
        case CBT_UI8:    return utils::ScalarType::U8;
        case CBT_I8:     return utils::ScalarType::I8;
        case CBT_UI16:   return utils::ScalarType::U16;
        case CBT_I16:    return utils::ScalarType::I16;
        case CBT_UI32:   return utils::ScalarType::U32;
        case CBT_I32:    return utils::ScalarType::I32;
        case CBT_UI64:   return utils::ScalarType::U64;
        case CBT_I64:    return utils::ScalarType::I64;
        case CBT_FLOAT:  return utils::ScalarType::Float;
        case CBT_DOUBLE: return utils::ScalarType::Double;
        case CBT_INVALID: break;
    }
    return utils::ScalarType::None;
}

/* Type dispatch. Caller passes a generic lambda: [&]<typename T>() { ... }
   The switch itself lives in utils/ScalarTypeDispatch.hpp - Modbus had a
   character-for-character copy of it. */
template <typename F>
void DispatchBitfieldType(CanBitfieldType type, F&& fn)
{
    utils::DispatchScalarType(ScalarTypeOf(type), std::forward<F>(fn));
}

class CanEntryBase
{
public:
    CanEntryBase() = default;
    CanEntryBase(uint8_t* data_, uint8_t data_len)
    {
        if(data_ && data_len)
            data.insert(data.end(), data_, data_ + data_len);
    }
    /* Copying is a copy. This used to be spelled out by hand and dropped
       last_execution; see CanTxEntry::Duplicate for why that mattered. */
    CanEntryBase(const CanEntryBase&) = default;
    CanEntryBase& operator=(const CanEntryBase&) = default;

    std::vector<uint8_t> data{};
    std::chrono::steady_clock::time_point last_execution;
};

class CanEntryTransmitInfo
{
public:
    CanEntryTransmitInfo() = default;
    CanEntryTransmitInfo(uint32_t period_, uint8_t log_level_, uint8_t favourite_level_) :
        period(period_), log_level(log_level_), favourite_level(favourite_level_) {}
    /* Was hand-written and silently left `count` at zero, so a copy reported a
       send history it did not have. Resetting the counter is what duplicating
       an entry wants, not what copying one means; CanTxEntry::Duplicate does it
       deliberately instead. */
    CanEntryTransmitInfo(const CanEntryTransmitInfo&) = default;
    CanEntryTransmitInfo& operator=(const CanEntryTransmitInfo&) = default;

    uint32_t period{};
    size_t count{};
    uint8_t log_level{};
    uint8_t favourite_level{};
};

class CanTxEntry : public CanEntryBase, public CanEntryTransmitInfo
{
public:
    CanTxEntry() = default;
    CanTxEntry(uint32_t id_, uint8_t* data_, uint8_t data_len_, uint32_t period_, uint8_t log_level_, uint8_t favourite_level_,
        const std::string& comment_, std::optional<uint32_t> color_, std::optional<uint32_t> bg_color_,
        std::optional<bool> is_bold_, std::optional<float> scale = {}, std::optional<std::string> font_face = {}) :
        CanEntryBase(data_, data_len_), CanEntryTransmitInfo(period_, log_level_, favourite_level_),
        id(id_), comment(comment_), m_color(color_), m_bg_color(bg_color_)
    {
        if(is_bold_.has_value()) m_is_bold = *is_bold_;
        if(scale.has_value())    m_scale    = *scale;
        if(font_face.has_value()) m_font_face = *font_face;
    }

    ~CanTxEntry() = default;

    /* A copy is a copy. The hand-written version this replaces incremented the
       frame ID and dropped m_scale and m_font_face, so any copy produced an
       entry addressed to a different CAN frame with half its styling gone. */
    CanTxEntry(const CanTxEntry&) = default;
    CanTxEntry& operator=(const CanTxEntry&) = default;

    // !\brief A new entry seeded from this one, as the TX list's Copy button
    // makes it.
    //
    // Duplicating carries the frame's configuration and appearance but not its
    // runtime state: the copy gets the next frame ID, has never been sent, and
    // is not scheduled. That renumbering used to live in the copy constructor,
    // where it fired on every copy rather than only on the one operation that
    // actually wants it.
    [[nodiscard]] CanTxEntry Duplicate() const
    {
        CanTxEntry copy(*this);
        copy.id = id + 1;
        copy.count = 0;
        copy.last_execution = {};
        copy.send = false;
        copy.single_shot = false;
        return copy;
    }

    uint32_t id{};
    std::string comment{};
    bool send{ false };
    bool single_shot{ false };
    std::optional<uint32_t> m_color;
    std::optional<uint32_t> m_bg_color;
    bool m_is_bold{ false };
    float m_scale{ 1.0f };
    std::string m_font_face;
};

class CanRxData : public CanEntryBase, public CanEntryTransmitInfo
{
public:
    CanRxData(uint8_t* data_, uint8_t data_len) : CanEntryBase(data_, data_len) { count = 1; }
};

class CanLogEntry : public CanEntryBase
{
public:
    // !\brief Mask of an extended (29-bit) CAN identifier.
    static constexpr uint32_t kFrameIdMask = 0x1FFFFFFF;

    CanLogEntry(uint8_t dir, uint32_t frame_id_, uint8_t* data_, uint8_t data_len, const std::chrono::steady_clock::time_point& timepoint) :
        CanEntryBase(data_, data_len), frame_id(frame_id_ & kFrameIdMask), direction(dir & 1U)
    {
        last_execution = timepoint;
    }

    /* These were a nameless union over a bitfield struct, so that the pair could
       also be read as one packed uint32_t. Nothing ever read it that way, and
       the nameless struct is a Microsoft extension that /W4 rejects, so the two
       values are simply two values. The masking was already explicit. */
    uint32_t frame_id{};
    uint8_t  direction{};  /* CAN_LOG_DIR_TX / CAN_LOG_DIR_RX */
};

// The five presentation fields were declared here by hand, identical in type
// and default to TextStyle - which Command, ModbusMap and DataEntryPresentation
// all inherit. A fifth copy is a fifth thing to keep in step.
class CanMap : public TextStyle
{
public:
    CanMap(const std::string& name, CanBitfieldType type, uint8_t size, int64_t min_val, int64_t max_val,
        const std::string& description, uint32_t color, uint32_t bg_color, bool is_bold, float scale) :
        TextStyle(color, bg_color, is_bold, scale),
        m_Type(type), m_Name(name), m_Size(size), m_MinVal(min_val), m_MaxVal(max_val), m_Description(description) {}

    CanBitfieldType m_Type;
    std::string     m_Name;
    uint8_t         m_Size;
    int64_t         m_MinVal;
    int64_t         m_MaxVal;
    std::string     m_Description;
};

using CanBitfieldInfo = std::vector<std::tuple<std::string, std::string, CanMap*>>;
