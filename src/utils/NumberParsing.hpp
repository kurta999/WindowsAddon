#pragma once

// !\brief Non-throwing numeric parsing.
//
// This lived in Utils.hpp, a 780-line grab bag that also pulls
// <boost/algorithm/hex.hpp>. The dependency-free core build - the one CI runs
// on Ubuntu with no Boost installed at all - compiles no source that includes
// Boost, so a test for these helpers could not include Utils.hpp to reach them.
// Parsing a number needs nothing but the standard library, so it says so.

#include <charconv>
#include <cstring>
#include <format>
#include <limits>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>

namespace utils
{
    template<class> inline constexpr bool always_false_v = false;

    // !\brief How much of the input a parse is allowed to leave behind.
    enum class ParseMode
    {
        // !\brief The whole (trimmed) input must be the number. "12G" fails.
        Whole,
        // !\brief Parse the leading number and ignore the rest. "12G" gives 12.
        Prefix,
    };

    namespace detail
    {
        /* char and bool have no from_chars overload; parse them through a wider
           integer and range-check the result. */
        template <typename R>
        using ParseTypeFor = std::conditional_t<
            std::is_same_v<R, bool> || std::is_same_v<R, char> ||
            std::is_same_v<R, signed char> || std::is_same_v<R, unsigned char>,
            std::conditional_t<std::is_signed_v<R>, int, unsigned int>, R>;

        [[nodiscard]] inline std::string_view TrimView(std::string_view text)
        {
            const auto first = text.find_first_not_of(" \t\r\n");
            if(first == std::string_view::npos)
                return {};
            return text.substr(first, text.find_last_not_of(" \t\r\n") - first + 1);
        }
    }

    // !\brief Parses `text` as R, or nothing when it is not a valid R.
    //
    // The non-throwing counterpart of `stoi`. Roughly half of this project's
    // numeric parsing came from a wxWidgets cell or dialog the user can type
    // anything into: `std::stoi` throws `std::invalid_argument` on that input,
    // and it was being called straight out of event handlers. Around forty-five
    // call sites wrapped it in the same ten-line try/catch/`bool parsed` shell
    // to survive; the rest simply did not, and threw through wx.
    //
    // Returning an optional is what lets a caller say "leave the old value
    // alone" in one line. `FrameIdAt` is the same idea specialised to the CAN
    // ID column, and this is that idea for every other cell.
    //
    // ParseMode::Whole rejects trailing input, so "12G" is not silently 0x12 -
    // that truncation is documented in architecture.md as a real bug. Use
    // ParseMode::Prefix only where a trailing tail is part of the format.
    template <typename R>
    [[nodiscard]] inline std::optional<R> TryParse(std::string_view text,
        ParseMode mode = ParseMode::Whole, int base = 10)
    {
        static_assert(std::is_arithmetic_v<R>, "R is not arithmetic!");

        const std::string_view trimmed = detail::TrimView(text);
        if(trimmed.empty())
            return std::nullopt;

        using ParseType = detail::ParseTypeFor<R>;
        ParseType parsed{};
        const char* const begin = trimmed.data();
        const char* const end = begin + trimmed.size();

        std::from_chars_result result{};
        if constexpr(std::is_floating_point_v<ParseType>)
            result = std::from_chars(begin, end, parsed);
        else
            result = std::from_chars(begin, end, parsed, base);

        if(result.ec != std::errc())
            return std::nullopt;
        if(mode == ParseMode::Whole && result.ptr != end)
            return std::nullopt;

        if constexpr(!std::is_same_v<ParseType, R>)
        {
            if(parsed < static_cast<ParseType>(std::numeric_limits<R>::lowest()) ||
               parsed > static_cast<ParseType>(std::numeric_limits<R>::max()))
                return std::nullopt;
        }
        return static_cast<R>(parsed);
    }

    // !\brief Parses `text` as R, falling back to `fallback` when it will not
    // parse. For the many call sites whose whole error handling was "keep what
    // was there before".
    template <typename R>
    [[nodiscard]] inline R ParseOr(std::string_view text, R fallback,
        ParseMode mode = ParseMode::Whole, int base = 10)
    {
        return TryParse<R>(text, mode, base).value_or(fallback);
    }

    // Parses `from_str` as R. Unlike the previous implementation this actually
    // parses at R's width, so 64-bit values are not silently truncated and
    // out-of-range input is reported instead of wrapping.
    //
    // This is ParseMode::Prefix on purpose, and changing that will break
    // settings loading. Settings are read through
    // boost::property_tree::ini_parser, which does *not* strip an inline
    // comment from a value: `Enable = 0 # Toggle TCP server` arrives here as
    // the string "0 # Toggle TCP server", and settings.example.ini comments
    // roughly a third of its keys that way. Only the leading number is meant.
    //
    // Prefer TryParse for anything a user can type into. This overload throws,
    // which is right for a malformed settings file - Settings::LoadSettings
    // catches per binding and reports which key failed - and wrong for a grid
    // cell, where the exception escapes through wxWidgets.
    template <typename R, typename S> inline R stoi(const S& from_str)
    {
        static_assert(std::is_arithmetic_v<R>, "R is not arithmetic!");

        using T = std::decay_t<decltype(from_str)>;

        std::string_view text;
        if constexpr(std::is_same_v<T, std::string> || std::is_same_v<T, std::string_view>)
            text = from_str;
        else if constexpr(std::is_same_v<T, const char*> || std::is_same_v<T, char*>)
            text = from_str ? std::string_view(from_str) : std::string_view{};
        else
            static_assert(always_false_v<T>, "bad type - from_str!");

        const std::optional<R> parsed = TryParse<R>(text, ParseMode::Prefix);
        if(!parsed)
            throw std::runtime_error(std::format("Bad stoi input ({})", text));

        return *parsed;
    }

    /* Anything that is not "false", not starting with '0' and not blank counts
       as true. A key that is present but has no value reads as false rather
       than true - a setting the user cleared is a setting they turned off. */
    template <typename S> inline bool stob(const S& from_str)
    {
        using T = std::decay_t<decltype(from_str)>;
        if constexpr(std::is_same_v<T, std::string>)
        {
            return !(from_str.empty() || from_str == "false" || from_str[0] == '0');
        }
        else if constexpr(std::is_same_v<T, const char*>)
        {
            if(from_str == nullptr || from_str[0] == '\0')
                return false;

            return !(!strcmp(from_str, "false") || from_str[0] == '0');
        }
        else
            static_assert(always_false_v<T>, "bad type - from_str!");
    }

}
