#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace can_codec
{
constexpr std::uint32_t Stm32SendMagic = 0xAABBCCDD;
constexpr std::uint32_t Stm32ReceiveMagic = 0xAABBCCDE;
constexpr std::size_t Stm32WireSize = 19;

struct Frame
{
    std::uint32_t id{};
    std::vector<std::uint8_t> data;

    bool operator==(const Frame&) const = default;
};

[[nodiscard]] std::uint16_t ModbusCrc(std::span<const std::uint8_t> bytes);
[[nodiscard]] std::optional<std::array<std::uint8_t, Stm32WireSize>> EncodeStm32(const Frame& frame);

class Stm32StreamDecoder
{
public:
    [[nodiscard]] std::vector<Frame> Feed(std::span<const std::uint8_t> bytes);
    [[nodiscard]] std::size_t BufferedBytes() const { return m_buffer.size(); }

private:
    std::vector<std::uint8_t> m_buffer;
};

[[nodiscard]] std::optional<std::string> EncodeLawicel(const Frame& frame);

class LawicelStreamDecoder
{
public:
    [[nodiscard]] std::vector<Frame> Feed(std::string_view bytes);
    [[nodiscard]] std::size_t BufferedBytes() const { return m_buffer.size(); }

private:
    static std::optional<Frame> ParseLine(std::string_view line);
    std::string m_buffer;
};
}
