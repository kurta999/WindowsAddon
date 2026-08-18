#include "CanCodecs.hpp"

#include <algorithm>
#include <charconv>
#include <cctype>

namespace can_codec
{
namespace
{
void WriteU32Le(std::uint8_t* out, std::uint32_t value)
{
    for(unsigned i = 0; i < 4; ++i)
        out[i] = static_cast<std::uint8_t>(value >> (i * 8));
}

std::uint32_t ReadU32Le(const std::uint8_t* bytes)
{
    return static_cast<std::uint32_t>(bytes[0]) |
           (static_cast<std::uint32_t>(bytes[1]) << 8) |
           (static_cast<std::uint32_t>(bytes[2]) << 16) |
           (static_cast<std::uint32_t>(bytes[3]) << 24);
}

int HexValue(char value)
{
    if(value >= '0' && value <= '9') return value - '0';
    if(value >= 'a' && value <= 'f') return value - 'a' + 10;
    if(value >= 'A' && value <= 'F') return value - 'A' + 10;
    return -1;
}

bool ParseHex(std::string_view text, std::uint32_t& value)
{
    value = 0;
    if(text.empty()) return false;
    for(const char ch : text)
    {
        const int digit = HexValue(ch);
        if(digit < 0) return false;
        value = (value << 4) | static_cast<std::uint32_t>(digit);
    }
    return true;
}
}

std::uint16_t ModbusCrc(std::span<const std::uint8_t> bytes)
{
    std::uint16_t crc = 0xFFFF;
    for(const auto byte : bytes)
    {
        crc ^= byte;
        for(int bit = 0; bit < 8; ++bit)
            crc = (crc & 1) ? static_cast<std::uint16_t>((crc >> 1) ^ 0xA001) : static_cast<std::uint16_t>(crc >> 1);
    }
    return crc;
}

std::optional<std::array<std::uint8_t, Stm32WireSize>> EncodeStm32(const Frame& frame)
{
    if(frame.data.size() > 8 || frame.id > 0x1FFFFFFF)
        return std::nullopt;

    std::array<std::uint8_t, Stm32WireSize> result{};
    WriteU32Le(result.data(), Stm32SendMagic);
    WriteU32Le(result.data() + 4, frame.id);
    result[8] = static_cast<std::uint8_t>(frame.data.size());
    std::copy(frame.data.begin(), frame.data.end(), result.begin() + 9);
    const auto crc = ModbusCrc(std::span<const std::uint8_t>(result).first(17));
    result[17] = static_cast<std::uint8_t>(crc);
    result[18] = static_cast<std::uint8_t>(crc >> 8);
    return result;
}

std::vector<Frame> Stm32StreamDecoder::Feed(std::span<const std::uint8_t> bytes)
{
    m_buffer.insert(m_buffer.end(), bytes.begin(), bytes.end());
    std::vector<Frame> frames;

    while(m_buffer.size() >= 4)
    {
        if(ReadU32Le(m_buffer.data()) != Stm32ReceiveMagic)
        {
            m_buffer.erase(m_buffer.begin());
            continue;
        }
        if(m_buffer.size() < Stm32WireSize)
            break;

        const auto length = m_buffer[8];
        const auto expected_crc = static_cast<std::uint16_t>(m_buffer[17]) |
                                  (static_cast<std::uint16_t>(m_buffer[18]) << 8);
        const auto actual_crc = ModbusCrc(std::span<const std::uint8_t>(m_buffer).first(17));
        if(length > 8 || expected_crc != actual_crc)
        {
            // Advance one byte so a valid frame immediately following corrupt
            // input can still be found.
            m_buffer.erase(m_buffer.begin());
            continue;
        }

        Frame frame;
        frame.id = ReadU32Le(m_buffer.data() + 4);
        frame.data.assign(m_buffer.begin() + 9, m_buffer.begin() + 9 + length);
        frames.push_back(std::move(frame));
        m_buffer.erase(m_buffer.begin(), m_buffer.begin() + Stm32WireSize);
    }
    return frames;
}

std::optional<std::string> EncodeLawicel(const Frame& frame)
{
    if(frame.data.size() > 8 || frame.id > 0x1FFFFFFF)
        return std::nullopt;

    const bool extended = frame.id > 0x7FF;
    char id_buffer[8]{};
    const auto conversion = std::to_chars(id_buffer, id_buffer + sizeof(id_buffer), frame.id, 16);
    const std::size_t id_length = static_cast<std::size_t>(conversion.ptr - id_buffer);
    const std::size_t id_width = extended ? 8 : 3;
    std::string id(id_width - id_length, '0');
    for(char* character = id_buffer; character != conversion.ptr; ++character)
        id.push_back(static_cast<char>(std::toupper(static_cast<unsigned char>(*character))));

    constexpr char digits[] = "0123456789ABCDEF";
    std::string result;
    result.reserve(1 + (extended ? 8 : 3) + 1 + frame.data.size() * 2 + 1);
    result.push_back(extended ? 'T' : 't');
    result += id;
    result.push_back(static_cast<char>('0' + frame.data.size()));
    for(const auto byte : frame.data)
    {
        result.push_back(digits[byte >> 4]);
        result.push_back(digits[byte & 0x0F]);
    }
    result.push_back('\r');
    return result;
}

std::vector<Frame> LawicelStreamDecoder::Feed(std::string_view bytes)
{
    m_buffer.append(bytes);
    std::vector<Frame> frames;

    while(true)
    {
        const auto start = m_buffer.find_first_of("tTV");
        if(start == std::string::npos)
        {
            // Noise cannot begin a later frame. Retain only a bounded tail.
            if(m_buffer.size() > 64) m_buffer.clear();
            break;
        }
        if(start > 0)
            m_buffer.erase(0, start);

        const auto end = m_buffer.find('\r');
        if(end == std::string::npos)
        {
            if(m_buffer.size() > 64)
            {
                m_buffer.erase(0, 1);
                continue;
            }
            break;
        }

        const std::string line = m_buffer.substr(0, end);
        m_buffer.erase(0, end + 1);
        if(line.starts_with('V'))
            continue;
        if(auto frame = ParseLine(line))
            frames.push_back(std::move(*frame));
    }
    return frames;
}

std::optional<Frame> LawicelStreamDecoder::ParseLine(std::string_view line)
{
    if(line.empty() || (line[0] != 't' && line[0] != 'T'))
        return std::nullopt;
    const std::size_t id_width = line[0] == 't' ? 3 : 8;
    if(line.size() < 1 + id_width + 1)
        return std::nullopt;

    std::uint32_t id = 0;
    if(!ParseHex(line.substr(1, id_width), id))
        return std::nullopt;
    if((id_width == 3 && id > 0x7FF) || id > 0x1FFFFFFF)
        return std::nullopt;

    const char length_char = line[1 + id_width];
    if(length_char < '0' || length_char > '8')
        return std::nullopt;
    const std::size_t length = static_cast<std::size_t>(length_char - '0');
    if(line.size() != 1 + id_width + 1 + length * 2)
        return std::nullopt;

    Frame frame{id, {}};
    frame.data.reserve(length);
    for(std::size_t index = 0; index < length; ++index)
    {
        const int high = HexValue(line[1 + id_width + 1 + index * 2]);
        const int low = HexValue(line[1 + id_width + 2 + index * 2]);
        if(high < 0 || low < 0)
            return std::nullopt;
        frame.data.push_back(static_cast<std::uint8_t>((high << 4) | low));
    }
    return frame;
}
}
