#pragma once

#include "ModbusProtocol.hpp"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <expected>
#include <iomanip>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

namespace modbus_custom_command
{
enum class CheckType : uint8_t { None, Crc, Lrc };

inline bool IsSeparator(char ch)
{
    return std::isspace(static_cast<unsigned char>(ch)) || ch == ',' || ch == ';' || ch == ':';
}

inline std::expected<std::vector<uint8_t>, std::string> ParseHexBytes(std::string_view text)
{
    std::vector<uint8_t> bytes;
    size_t pos = 0;
    while(pos < text.size())
    {
        while(pos < text.size() && IsSeparator(text[pos]))
            ++pos;
        if(pos == text.size())
            break;
        size_t end = pos;
        while(end < text.size() && !IsSeparator(text[end]))
            ++end;
        std::string token(text.substr(pos, end - pos));
        if(token.starts_with("0x") || token.starts_with("0X"))
            token.erase(0, 2);
        if(token.empty() || !std::ranges::all_of(token, [](char c)
            { return std::isxdigit(static_cast<unsigned char>(c)) != 0; }))
            return std::unexpected("Invalid hex byte: " + token);
        if(token.size() > 2 && token.size() % 2 != 0)
            return std::unexpected("Compact hex strings must contain an even number of digits: " + token);
        const size_t width = token.size() > 2 ? 2 : token.size();
        for(size_t i = 0; i < token.size(); i += width)
        {
            unsigned int value = 0;
            const auto* first = token.data() + i;
            const auto* last = first + width;
            const auto parsed = std::from_chars(first, last, value, 16);
            if(parsed.ec != std::errc{} || value > 0xFF)
                return std::unexpected("Invalid hex byte: " + token.substr(i, width));
            bytes.push_back(static_cast<uint8_t>(value));
        }
        pos = end;
    }
    if(bytes.empty())
        return std::unexpected("Enter at least one hex byte.");
    return bytes;
}

inline uint8_t CalculateLrc(const std::vector<uint8_t>& bytes)
{
    uint8_t sum = 0;
    for(uint8_t byte : bytes)
        sum = static_cast<uint8_t>(sum + byte);
    return static_cast<uint8_t>(-sum);
}

inline void AppendCheck(std::vector<uint8_t>& bytes, CheckType check_type)
{
    if(check_type == CheckType::Crc)
        modbus::AppendCrc(bytes);
    else if(check_type == CheckType::Lrc)
        bytes.push_back(CalculateLrc(bytes));
}

inline std::string FormatHexBytes(const std::vector<uint8_t>& bytes)
{
    std::ostringstream stream;
    stream << std::uppercase << std::hex << std::setfill('0');
    for(size_t i = 0; i < bytes.size(); ++i)
    {
        if(i)
            stream << ' ';
        stream << std::setw(2) << static_cast<unsigned int>(bytes[i]);
    }
    return stream.str();
}
}
