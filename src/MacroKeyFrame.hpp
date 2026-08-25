#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>

// The frame the keypad firmware sends over the serial link.
//
// It is decoded here, away from the GUI, because it arrives from a device: the
// length is whatever the port happened to deliver, not what the frame claims,
// so every read has to be bounded by it. Keeping it headless also lets it be
// unit tested and fuzzed. See ADR-0003.
namespace macro_key_frame
{
// state + 8 modifiers + 6 keys + CRC16.
inline constexpr std::size_t frame_size = 17;
inline constexpr std::size_t key_count = 6;

// Sensor readings share the serial link and are recognised by this prefix.
inline constexpr std::string_view measurement_prefix = "MEAS_DATA";

// The firmware announces an unexpected restart with this frame.
inline constexpr std::string_view reset_prefix = "reset";
inline constexpr std::uint16_t reset_crc = 0x6bd8;

enum class ParseError : std::uint8_t
{
    None,
    WrongLength,
    Crc,
};

struct Frame
{
    std::uint8_t state = 0;
    bool lctrl = false;
    bool lshift = false;
    bool lalt = false;
    bool lgui = false;
    bool rctrl = false;
    bool rshift = false;
    bool ralt = false;
    bool rgui = false;
    std::array<std::uint8_t, key_count> keys{};
};

struct ParseResult
{
    ParseError error = ParseError::WrongLength;
    Frame frame;
    // Both are reported so a mismatch can be logged rather than only refused.
    std::uint16_t received_crc = 0;
    std::uint16_t computed_crc = 0;
};

[[nodiscard]] std::uint16_t Crc16Modbus(std::string_view data);

// !\brief Is this a sensor reading rather than a key frame?
// Safe for a frame shorter than the prefix, which is the case this exists for.
[[nodiscard]] bool IsMeasurementFrame(std::string_view data);

// !\brief Does this frame announce a firmware reset?
[[nodiscard]] bool IsResetFrame(std::string_view data);

// !\brief Decode and verify. The frame is only meaningful when error is None.
[[nodiscard]] ParseResult Parse(std::string_view data);

// !\brief Are no modifiers held and no keys down?
[[nodiscard]] bool IsAllReleased(const Frame& frame);
}
