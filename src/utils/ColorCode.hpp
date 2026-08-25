#pragma once

#include "utils/NumberParsing.hpp"

#include <array>
#include <cstdint>
#include <format>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

namespace utils
{
    // !\brief How a colour is written in this project's configuration files.
    //
    // The seven-name table and the two conversions lived twice: once here for
    // the XML side and once inside ModbusJsonPersistence for the JSON side. The
    // copies had already drifted - the same 0xFF0000 was written back as "red"
    // by one and "0xFF0000" by the other, so a device spelled its colours
    // differently depending on which format it came from.
    //
    // Nothing here logs, so it stays usable from targets that do not link the
    // logger. utils::ColorStringToInt is the reporting wrapper.
    inline constexpr std::array<std::pair<std::string_view, uint32_t>, 7> kColorNames{{
        { "red",    0xFF0000 },
        { "green",  0x33FF33 },
        { "blue",   0x6495ED },
        { "orange", 0xFF7F50 },
        { "white",  0xFFFFFF },
        { "black",  0x000000 },
        { "pink",   0xFF10F0 },
    }};

    // !\brief Read a colour name, "#RRGGBB", "0xRRGGBB" or bare "RRGGBB".
    // !\return Nothing when the text is neither a known name nor whole hex.
    [[nodiscard]] inline std::optional<uint32_t> ParseColor(std::string_view text)
    {
        for(const auto& [name, value] : kColorNames)
            if(name == text)
                return value;

        if(text.starts_with('#'))
            text.remove_prefix(1);
        else if(text.starts_with("0x") || text.starts_with("0X"))
            text.remove_prefix(2);

        /* Whole rather than prefix: sscanf("%x") stopped at the first character
           it could not use, so an unknown name that happens to start with hex
           digits - "chartreuse", "beef" - silently became a colour. */
        return TryParse<uint32_t>(text, ParseMode::Whole, 16);
    }

    // !\brief Write a colour back, preferring the name a reader would recognise.
    [[nodiscard]] inline std::string FormatColor(uint32_t value)
    {
        for(const auto& [name, named_value] : kColorNames)
            if(named_value == value)
                return std::string(name);
        return std::format("0x{:X}", value);
    }
}
