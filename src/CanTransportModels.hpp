#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>

constexpr std::size_t MAX_CAN_FRAME_DATA_LEN = 8;

enum class CanDeviceType
{
    STM32,
    LAWICEL
};

// Value object passed between the CAN transport and its protocol strategy.
// Keeping it outside CanSerialPort prevents protocol implementations from
// depending on the concrete transport singleton.
struct CanData
{
    CanData(std::uint32_t frame_id_value, std::uint8_t length, const std::uint8_t* source)
        : frame_id(frame_id_value), data_len(static_cast<std::uint8_t>(std::min<std::size_t>(length, sizeof(data))))
    {
        if(source != nullptr)
            std::memcpy(data, source, data_len);
    }

    std::uint32_t frame_id{};
    std::uint8_t data_len{};
    std::uint8_t data[MAX_CAN_FRAME_DATA_LEN]{};
};
