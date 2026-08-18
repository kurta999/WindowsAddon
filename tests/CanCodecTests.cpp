#include "TestFramework.hpp"

#include "CanCodecs.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <string>
#include <random>
#include <vector>

namespace
{
std::array<std::uint8_t, can_codec::Stm32WireSize> ReceiveWire(const can_codec::Frame& frame)
{
    auto wire = *can_codec::EncodeStm32(frame);
    wire[0] = 0xDE; // receive magic 0xAABBCCDE, little endian
    const auto crc = can_codec::ModbusCrc(std::span<const std::uint8_t>(wire).first(17));
    wire[17] = static_cast<std::uint8_t>(crc);
    wire[18] = static_cast<std::uint8_t>(crc >> 8);
    return wire;
}
}

TEST_CASE(Stm32CodecEncodesValidFrameAndCrc)
{
    const can_codec::Frame frame{0x18DAF110, {0x02, 0x10, 0x03}};
    const auto wire = can_codec::EncodeStm32(frame);
    EXPECT_TRUE(wire.has_value());
    EXPECT_EQ((*wire)[8], std::uint8_t{3});
    const auto crc = can_codec::ModbusCrc(std::span<const std::uint8_t>(*wire).first(17));
    EXPECT_EQ((*wire)[17], static_cast<std::uint8_t>(crc));
    EXPECT_EQ((*wire)[18], static_cast<std::uint8_t>(crc >> 8));
}

TEST_CASE(Stm32CodecRecoversAfterNoiseAndInvalidMagic)
{
    const auto valid = ReceiveWire({0x123, {1, 2, 3}});
    std::vector<std::uint8_t> bytes{0xAA, 0xBB, 0xCC, 0xDD, 0xDE, 0xAD};
    bytes.insert(bytes.end(), valid.begin(), valid.end());

    can_codec::Stm32StreamDecoder decoder;
    const auto frames = decoder.Feed(bytes);
    EXPECT_EQ(frames.size(), size_t{1});
    EXPECT_EQ(frames[0], (can_codec::Frame{0x123, {1, 2, 3}}));
}

TEST_CASE(Stm32CodecRejectsInvalidLengthAndBadCrc)
{
    auto invalid_length = ReceiveWire({0x123, {1}});
    invalid_length[8] = 9;
    auto crc = can_codec::ModbusCrc(std::span<const std::uint8_t>(invalid_length).first(17));
    invalid_length[17] = static_cast<std::uint8_t>(crc);
    invalid_length[18] = static_cast<std::uint8_t>(crc >> 8);

    auto bad_crc = ReceiveWire({0x456, {4, 5, 6}});
    bad_crc[17] ^= 0xFF;

    can_codec::Stm32StreamDecoder decoder;
    EXPECT_TRUE(decoder.Feed(invalid_length).empty());
    EXPECT_TRUE(decoder.Feed(bad_crc).empty());
    EXPECT_FALSE(can_codec::EncodeStm32({0x123, std::vector<std::uint8_t>(9)}).has_value());
}

TEST_CASE(Stm32CodecHandlesFragmentedAndMultipleFrames)
{
    const auto first = ReceiveWire({0x101, {1}});
    const auto second = ReceiveWire({0x102, {2, 3}});
    can_codec::Stm32StreamDecoder decoder;

    EXPECT_TRUE(decoder.Feed(std::span<const std::uint8_t>(first).first(7)).empty());
    std::vector<std::uint8_t> tail(first.begin() + 7, first.end());
    tail.insert(tail.end(), second.begin(), second.end());
    const auto frames = decoder.Feed(tail);
    EXPECT_EQ(frames.size(), size_t{2});
    EXPECT_EQ(frames[0].id, std::uint32_t{0x101});
    EXPECT_EQ(frames[1].id, std::uint32_t{0x102});
}

TEST_CASE(LawicelCodecSupportsStandardAndExtendedFrames)
{
    const auto standard = can_codec::EncodeLawicel({0x7A, {0x01, 0xAF}});
    const auto extended = can_codec::EncodeLawicel({0x18DAF110, {0x02, 0x10, 0x03}});
    EXPECT_EQ(*standard, std::string("t07A201AF\r"));
    EXPECT_EQ(*extended, std::string("T18DAF1103021003\r"));

    can_codec::LawicelStreamDecoder decoder;
    const auto frames = decoder.Feed(*standard + *extended);
    EXPECT_EQ(frames.size(), size_t{2});
    EXPECT_EQ(frames[0], (can_codec::Frame{0x7A, {0x01, 0xAF}}));
    EXPECT_EQ(frames[1], (can_codec::Frame{0x18DAF110, {0x02, 0x10, 0x03}}));
}

TEST_CASE(LawicelCodecHandlesFragmentationNoiseAndVersionLines)
{
    can_codec::LawicelStreamDecoder decoder;
    EXPECT_TRUE(decoder.Feed("noiseV1234\rt1232").empty());
    const auto frames = decoder.Feed("AABB\r");
    EXPECT_EQ(frames.size(), size_t{1});
    EXPECT_EQ(frames[0], (can_codec::Frame{0x123, {0xAA, 0xBB}}));
}

TEST_CASE(LawicelCodecRejectsMalformedTruncatedAndOversizedInput)
{
    can_codec::LawicelStreamDecoder decoder;
    EXPECT_TRUE(decoder.Feed("t12\r").empty());
    EXPECT_TRUE(decoder.Feed("t1239AABBCCDDEEFF001122\r").empty());
    EXPECT_TRUE(decoder.Feed("t1232GG00\r").empty());
    EXPECT_TRUE(decoder.Feed("TFFFFFFFF0\r").empty());
    EXPECT_FALSE(can_codec::EncodeLawicel({0x123, std::vector<std::uint8_t>(9)}).has_value());
}

TEST_CASE(CanCodecsRoundTripArbitraryFrames)
{
    std::mt19937 random(0xC0DEC0DEu);
    for(int iteration = 0; iteration < 1000; ++iteration)
    {
        can_codec::Frame expected;
        expected.id = random() & 0x1FFFFFFF;
        expected.data.resize(random() % 9);
        for(auto& byte : expected.data) byte = static_cast<std::uint8_t>(random());

        auto stm32 = ReceiveWire(expected);
        can_codec::Stm32StreamDecoder stm32_decoder;
        const auto stm32_frames = stm32_decoder.Feed(stm32);
        EXPECT_EQ(stm32_frames, std::vector<can_codec::Frame>{expected});

        const auto lawicel = can_codec::EncodeLawicel(expected);
        ASSERT_TRUE(lawicel.has_value());
        can_codec::LawicelStreamDecoder lawicel_decoder;
        const auto lawicel_frames = lawicel_decoder.Feed(*lawicel);
        EXPECT_EQ(lawicel_frames, std::vector<can_codec::Frame>{expected});
    }
}

TEST_CASE(CanStreamDecodersRemainBoundedUnderRandomNoise)
{
    std::mt19937 random(0xF022u);
    can_codec::Stm32StreamDecoder stm32;
    can_codec::LawicelStreamDecoder lawicel;
    for(int iteration = 0; iteration < 2000; ++iteration)
    {
        std::array<std::uint8_t, 13> bytes{};
        for(auto& byte : bytes) byte = static_cast<std::uint8_t>(random());
        const auto binary_frames = stm32.Feed(bytes);
        for(const auto& frame : binary_frames) EXPECT_LE(frame.data.size(), size_t{8});

        const std::string text(bytes.begin(), bytes.end());
        const auto text_frames = lawicel.Feed(text);
        for(const auto& frame : text_frames) EXPECT_LE(frame.data.size(), size_t{8});
        EXPECT_LE(stm32.BufferedBytes(), can_codec::Stm32WireSize - 1);
        EXPECT_LE(lawicel.BufferedBytes(), size_t{64});
    }
}
