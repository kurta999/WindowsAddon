#pragma once

#include <cstddef>
#include <optional>
#include <string_view>

// !\brief The `[({PARAM:default})]` placeholders a command string can carry.
//
// Three methods on Command scanned for them, with the same eleven lines each:
// find the closing marker, find the opening marker before it, take what lies
// between. They differ only in what they then do with it - collect the default,
// substitute a value for the whole placeholder, or write a value back over the
// default and keep the markers.
namespace command_params
{
    inline constexpr std::string_view kOpen = "[({PARAM:";
    inline constexpr std::string_view kClose = "})]";

    // !\brief Where one placeholder sits in the text.
    struct Placeholder
    {
        // !\brief Index of the opening marker.
        std::size_t begin = 0;

        // !\brief The default text between the markers.
        std::size_t value_begin = 0;
        std::size_t value_end = 0;

        // !\brief One past the closing marker.
        [[nodiscard]] constexpr std::size_t end() const { return value_end + kClose.size(); }

        [[nodiscard]] constexpr std::size_t value_length() const { return value_end - value_begin; }
    };

    // !\brief The next placeholder at or after `pos`, or nothing.
    //
    // The two offsets are the ones the three hand-written copies used, kept so
    // the scan behaves as it did: the closing marker is looked for from
    // `pos + 3`, and the opening marker from `pos - 1` within the text before
    // it. A caller walks the string by moving `pos` past what it just handled.
    [[nodiscard]] inline std::optional<Placeholder> Next(std::string_view text, std::size_t pos)
    {
        const std::size_t close = text.find(kClose, pos + 3);
        if(close == std::string_view::npos)
            return std::nullopt;

        const std::size_t from = pos == 0 ? 0 : pos - 1;
        const std::size_t open = text.substr(0, close).find(kOpen, from);
        if(open == std::string_view::npos)
            return std::nullopt;

        return Placeholder{ open, open + kOpen.size(), close };
    }
}
