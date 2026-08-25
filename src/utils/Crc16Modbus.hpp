#pragma once

#include <cstdint>
#include <span>

namespace utils
{
    // !\brief The Modbus CRC-16.
    //
    // Polynomial 0x8005 reflected (0xA001 applied on the way down), initial
    // value 0xFFFF, input and output reflected, no final xor.
    //
    // One definition for the whole tree: the Modbus framing code and the macro
    // keypad's frame parser each carried their own character-for-character copy
    // of this loop, next to a third, unused boost::crc_optimal spelling in
    // utils. Nothing tied the three together, so a fix to one would have missed
    // the others.
    [[nodiscard]] constexpr uint16_t Crc16Modbus(std::span<const uint8_t> data) noexcept
    {
        uint16_t crc = 0xFFFF;
        for(const uint8_t byte : data)
        {
            crc ^= byte;
            for(int bit = 0; bit < 8; ++bit)
            {
                const bool lsb = (crc & 1) != 0;
                crc >>= 1;
                if(lsb)
                    crc ^= 0xA001;
            }
        }
        return crc;
    }
}
