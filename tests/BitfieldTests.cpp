#include "TestFramework.hpp"

extern "C"
{
#include <bitfield/bitfield.h>
}

#include <array>
#include <cstdint>

TEST_CASE(BitfieldRoundTripsAcrossByteBoundaries)
{
    constexpr std::array<uint16_t, 8> widths{1, 4, 7, 8, 12, 16, 31, 64};

    for(const uint16_t width : widths)
    {
        for(uint16_t offset = 0; offset + width <= 80; ++offset)
        {
            std::array<uint8_t, 10> bytes{};
            const uint64_t value = width == 64
                ? UINT64_C(0xFEDCBA9876543210)
                : bitmask(static_cast<uint8_t>(width)) ^ (bitmask(static_cast<uint8_t>(width)) >> 2);

            EXPECT_TRUE(set_bitfield(value, offset, width, bytes.data(), static_cast<uint16_t>(bytes.size())));
            EXPECT_EQ(get_bitfield(bytes.data(), static_cast<uint16_t>(bytes.size()), offset, width), value);
        }
    }
}

TEST_CASE(BitfieldWritesPreserveSurroundingBits)
{
    std::array<uint8_t, 2> bytes{0xAA, 0x55};

    EXPECT_TRUE(set_bitfield(0, 4, 8, bytes.data(), static_cast<uint16_t>(bytes.size())));
    EXPECT_EQ(static_cast<unsigned>(bytes[0]), 0xA0u);
    EXPECT_EQ(static_cast<unsigned>(bytes[1]), 0x05u);
}

TEST_CASE(BitfieldRejectsOverflowAndOutOfBoundsWrites)
{
    std::array<uint8_t, 2> bytes{0x12, 0x34};
    const auto original = bytes;

    EXPECT_FALSE(set_bitfield(0x10, 0, 4, bytes.data(), static_cast<uint16_t>(bytes.size())));
    EXPECT_TRUE(bytes == original);
    EXPECT_FALSE(set_bitfield(1, 16, 1, bytes.data(), static_cast<uint16_t>(bytes.size())));
    EXPECT_TRUE(bytes == original);
}

TEST_CASE(BitCountConversionSupportsIsoTpSizedPayloads)
{
    EXPECT_EQ(bits_to_bytes(32768), static_cast<uint16_t>(4096));
    EXPECT_EQ(bits_to_bytes(32767), static_cast<uint16_t>(4096));
}
