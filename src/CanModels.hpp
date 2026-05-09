#pragma once

#include <vector>
#include <chrono>
#include <string>
#include <optional>
#include <cstdint>

constexpr uint8_t CAN_LOG_DIR_TX = 0;
constexpr uint8_t CAN_LOG_DIR_RX = 1;
constexpr size_t MAX_ISOTP_FRAME_LEN = 4096;

enum CanBitfieldType : uint8_t
{
    CBT_BOOL, CBT_UI8, CBT_I8, CBT_UI16, CBT_I16, CBT_UI32, CBT_I32, CBT_UI64, CBT_I64, CBT_FLOAT, CBT_DOUBLE, CBT_INVALID
};

/* Type-dispatch helper — replaces duplicated switch blocks across the codebase.
   Caller passes a generic lambda: [&]<typename T>() { ... } */
template <typename F>
void DispatchBitfieldType(CanBitfieldType type, F&& fn)
{
    switch(type)
    {
        case CBT_BOOL:
        case CBT_UI8:    std::forward<F>(fn).template operator()<uint8_t>();  break;
        case CBT_I8:     std::forward<F>(fn).template operator()<int8_t>();   break;
        case CBT_UI16:   std::forward<F>(fn).template operator()<uint16_t>(); break;
        case CBT_I16:    std::forward<F>(fn).template operator()<int16_t>();  break;
        case CBT_UI32:   std::forward<F>(fn).template operator()<uint32_t>(); break;
        case CBT_I32:    std::forward<F>(fn).template operator()<int32_t>();  break;
        case CBT_UI64:   std::forward<F>(fn).template operator()<uint64_t>(); break;
        case CBT_I64:    std::forward<F>(fn).template operator()<int64_t>();  break;
        case CBT_FLOAT:  std::forward<F>(fn).template operator()<float>();    break;
        case CBT_DOUBLE: std::forward<F>(fn).template operator()<double>();   break;
        default: break;
    }
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
    CanEntryBase(const CanEntryBase& from) : data(from.data) {}

    std::vector<uint8_t> data{};
    std::chrono::steady_clock::time_point last_execution;
};

class CanEntryTransmitInfo
{
public:
    CanEntryTransmitInfo() = default;
    CanEntryTransmitInfo(uint32_t period_, uint8_t log_level_, uint8_t favourite_level_) :
        period(period_), log_level(log_level_), favourite_level(favourite_level_) {}
    CanEntryTransmitInfo(const CanEntryTransmitInfo& from) :
        period(from.period), log_level(from.log_level), favourite_level(from.favourite_level) {}

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
        id(id_), CanEntryBase(data_, data_len_), CanEntryTransmitInfo(period_, log_level_, favourite_level_),
        comment(comment_), m_color(color_), m_bg_color(bg_color_)
    {
        if(is_bold_.has_value()) m_is_bold = *is_bold_;
        if(scale.has_value())    m_scale    = *scale;
        if(font_face.has_value()) m_font_face = *font_face;
    }

    ~CanTxEntry() = default;
    CanTxEntry(const CanTxEntry& from) :
        CanEntryBase(from), id(from.id + 1), CanEntryTransmitInfo(from),
        comment(from.comment), m_color(from.m_color), m_bg_color(from.m_bg_color), m_is_bold(from.m_is_bold) {}

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
    CanLogEntry(uint8_t dir, uint32_t frame_id_, uint8_t* data_, uint8_t data_len, std::chrono::steady_clock::time_point& timepoint) :
        CanEntryBase(data_, data_len)
    {
        frame_id  = frame_id_ & 0x1FFFFFFF;
        direction = dir & 1;
        last_execution = timepoint;
    }
    union
    {
        uint32_t frame_id_and_direction = 0;
        struct
        {
            uint32_t frame_id : 29;
            uint8_t  direction : 1;  /* 0 = sent, 1 = received */
        };
    };
};

class CanMap
{
public:
    CanMap(const std::string& name, CanBitfieldType type, uint8_t size, int64_t min_val, int64_t max_val,
        const std::string& description, uint32_t color, uint32_t bg_color, bool is_bold, float scale) :
        m_Name(name), m_Type(type), m_Size(size), m_MinVal(min_val), m_MaxVal(max_val), m_Description(description),
        m_color(color), m_bg_color(bg_color), m_is_bold(is_bold), m_scale(scale) {}

    CanBitfieldType m_Type;
    std::string     m_Name;
    uint8_t         m_Size;
    int64_t         m_MinVal;
    int64_t         m_MaxVal;
    std::string     m_Description;

    /* Visual customization (formerly from BasicGuiTextCustomization) */
    uint32_t    m_color{};
    uint32_t    m_bg_color{};
    bool        m_is_bold{ false };
    float       m_scale{ 1.0f };
    std::string m_font_face;
};

using CanBitfieldInfo = std::vector<std::tuple<std::string, std::string, CanMap*>>;
