#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace utils
{
    // !\brief Removes the separators people type between hex digits.
    //
    // Spaces everywhere, dots in the raw UDS dialog alone - so "AA.BB" was
    // valid in one field and rejected by the next. Both go here.
    [[nodiscard]] inline std::string StripHexSeparators(std::string_view text)
    {
        std::string digits;
        digits.reserve(text.size());
        for(const char c : text)
        {
            if(c != ' ' && c != '.')
                digits.push_back(c);
        }
        return digits;
    }

    // !\brief The value of one hex digit, or nothing if it is not one.
    [[nodiscard]] constexpr std::optional<uint8_t> HexDigit(char c) noexcept
    {
        if(c >= '0' && c <= '9')
            return static_cast<uint8_t>(c - '0');
        if(c >= 'a' && c <= 'f')
            return static_cast<uint8_t>(c - 'a' + 10);
        if(c >= 'A' && c <= 'F')
            return static_cast<uint8_t>(c - 'A' + 10);
        return std::nullopt;
    }

    // !\brief Bytes from hex text a person typed.
    //
    // Four call sites - the CAN data-frame dialog, the TX grid's data cell, the
    // raw UDS dialog and the DID write - each wrote the same three steps: strip
    // the separators, decode into a fixed stack buffer, log and give up if the
    // decode came back empty.
    //
    // The byte count comes from what was decoded rather than from the length of
    // the input. Deriving it from the text is what once put uninitialised stack
    // bytes on the CAN bus, and returning the bytes rather than filling a
    // caller's array means there is no uninitialised buffer left to send.
    //
    // Returns nothing for text that is not valid hex, for an odd number of
    // digits, and for input that is empty once the separators are gone. Longer
    // input than `max_bytes` is cut, which is what the grid cell already did.
    // The caller logs: each site names a different thing it is skipping.
    //
    // Decoding by hand rather than through boost::algorithm::unhex keeps this
    // header usable from the dependency-free test build, which is where its
    // cases are pinned.
    [[nodiscard]] inline std::optional<std::vector<uint8_t>> ParseHexBytes(
        std::string_view text, std::size_t max_bytes)
    {
        std::string digits = StripHexSeparators(text);

        if(digits.empty() || digits.size() % 2 != 0)
            return std::nullopt;

        if(digits.size() > max_bytes * 2)
            digits.resize(max_bytes * 2);

        std::vector<uint8_t> bytes;
        bytes.reserve(digits.size() / 2);
        for(std::size_t i = 0; i < digits.size(); i += 2)
        {
            const auto high = HexDigit(digits[i]);
            const auto low = HexDigit(digits[i + 1]);
            if(!high || !low)
                return std::nullopt;

            bytes.push_back(static_cast<uint8_t>((*high << 4) | *low));
        }

        return bytes;
    }
}
