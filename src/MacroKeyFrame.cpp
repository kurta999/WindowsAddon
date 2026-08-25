#include "MacroKeyFrame.hpp"

#include "utils/Crc16Modbus.hpp"

#include <algorithm>
#include <span>

namespace macro_key_frame
{
namespace
{
[[nodiscard]] std::uint8_t ByteAt(std::string_view data, std::size_t index)
{
    return static_cast<std::uint8_t>(data[index]);
}
}

std::uint16_t Crc16Modbus(std::string_view data)
{
    return utils::Crc16Modbus(
        std::span<const std::uint8_t>(reinterpret_cast<const std::uint8_t*>(data.data()), data.size()));
}

bool IsMeasurementFrame(std::string_view data)
{
    return data.starts_with(measurement_prefix);
}

bool IsResetFrame(std::string_view data)
{
    if(data.size() != frame_size || !data.starts_with(reset_prefix))
        return false;
    return Crc16Modbus(data.substr(0, frame_size - sizeof(std::uint16_t))) == reset_crc;
}

ParseResult Parse(std::string_view data)
{
    ParseResult result;
    if(data.size() != frame_size)
        return result;

    constexpr std::size_t crc_offset = frame_size - sizeof(std::uint16_t);
    result.computed_crc = Crc16Modbus(data.substr(0, crc_offset));
    // Read explicitly rather than reinterpreting the bytes as a struct, so the
    // result does not depend on the host's byte order or alignment.
    result.received_crc = static_cast<std::uint16_t>(
        ByteAt(data, crc_offset) | (ByteAt(data, crc_offset + 1) << 8));

    if(result.received_crc != result.computed_crc)
    {
        result.error = ParseError::Crc;
        return result;
    }

    result.error = ParseError::None;
    result.frame.state = ByteAt(data, 0);
    result.frame.lctrl = ByteAt(data, 1) != 0;
    result.frame.lshift = ByteAt(data, 2) != 0;
    result.frame.lalt = ByteAt(data, 3) != 0;
    result.frame.lgui = ByteAt(data, 4) != 0;
    result.frame.rctrl = ByteAt(data, 5) != 0;
    result.frame.rshift = ByteAt(data, 6) != 0;
    result.frame.ralt = ByteAt(data, 7) != 0;
    result.frame.rgui = ByteAt(data, 8) != 0;
    for(std::size_t index = 0; index < key_count; ++index)
        result.frame.keys[index] = ByteAt(data, 9 + index);
    return result;
}

bool IsAllReleased(const Frame& frame)
{
    // The state byte is deliberately not part of this: the firmware uses it for
    // the report id, which is set even when nothing is held.
    return !frame.lctrl && !frame.lshift && !frame.lalt && !frame.lgui &&
           !frame.rctrl && !frame.rshift && !frame.ralt && !frame.rgui &&
           std::ranges::all_of(frame.keys, [](std::uint8_t key) { return key == 0; });
}
}
