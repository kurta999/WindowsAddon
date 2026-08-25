#pragma once

#include <cstddef>
#include <optional>
#include <string_view>

// !\brief Reading back the lines the logger wrote.
//
// This was Logger::SearchInLogFile: a log viewer living inside the log writer,
// which re-parsed the layout LOG_FILE_FORMAT writes with a twelve-field sscanf
// and a row of fixed char buffers.
//
// The buffers were the problem. The function field was `%255[^]]`, and MSVC
// writes decorated signatures - the longest in a recent run of the test suite
// is 629 characters, and twelve of sixty-five lines were over 255. For those,
// sscanf stopped at 255, failed to find the ']' it wanted next, returned 11
// instead of 12, and the line was skipped: a search that silently could not see
// a fifth of the log. Nothing reported it, because a skipped line and a line
// that did not match look the same.
//
// Splitting on delimiters instead has no width to exceed.
namespace log_file
{
// !\brief The fields of one written log line, as views into it.
//
// Views, so the line has to outlive them. Parse(SomethingReturningAString())
// compiles and dangles - which is how the tests for this first failed.
struct Line
{
    std::string_view level;
    std::string_view file;
    std::string_view function;
    std::string_view message;
};

// !\brief Split one line of logfile.txt into its fields.
//
// The layout is LOG_FILE_FORMAT: a timestamp, then "[level] [file:line -
// function] message". Recognising it requires that shape and a leading digit,
// so a message that itself contained a newline does not have its continuation
// mistaken for a record. The timestamp is not validated - nothing filters on
// it, and demanding seven parsable integers is what made the old parser
// brittle.
[[nodiscard]] constexpr std::optional<Line> Parse(std::string_view line)
{
    if(line.empty() || line.front() < '0' || line.front() > '9')
        return std::nullopt;

    const std::size_t level_open = line.find('[');
    if(level_open == std::string_view::npos)
        return std::nullopt;
    const std::size_t level_close = line.find(']', level_open + 1);
    if(level_close == std::string_view::npos)
        return std::nullopt;

    const std::size_t where_open = line.find('[', level_close + 1);
    if(where_open == std::string_view::npos)
        return std::nullopt;
    const std::size_t where_close = line.find(']', where_open + 1);
    if(where_close == std::string_view::npos)
        return std::nullopt;

    const std::string_view where = line.substr(where_open + 1, where_close - where_open - 1);
    const std::size_t colon = where.find(':');
    if(colon == std::string_view::npos)
        return std::nullopt;
    const std::size_t dash = where.find(" - ", colon + 1);
    if(dash == std::string_view::npos)
        return std::nullopt;

    Line parsed;
    parsed.level = line.substr(level_open + 1, level_close - level_open - 1);
    parsed.file = where.substr(0, colon);
    parsed.function = where.substr(dash + 3);

    const std::size_t message_start = where_close + 1;
    parsed.message = message_start < line.size()
        ? line.substr(message_start + (line[message_start] == ' ' ? 1 : 0))
        : std::string_view{};
    return parsed;
}

// !\brief Whether `haystack` contains `needle`, ignoring case.
//
// The old code reached for boost::algorithm::ifind_first for this; keeping it
// here leaves the parsing dependency-free and unit testable.
[[nodiscard]] constexpr bool ContainsIgnoringCase(std::string_view haystack, std::string_view needle)
{
    if(needle.empty())
        return true;
    if(needle.size() > haystack.size())
        return false;

    const auto lower = [](char c) constexpr
    {
        return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c;
    };

    for(std::size_t start = 0; start + needle.size() <= haystack.size(); ++start)
    {
        std::size_t i = 0;
        while(i < needle.size() && lower(haystack[start + i]) == lower(needle[i]))
            ++i;
        if(i == needle.size())
            return true;
    }
    return false;
}

// !\brief Whether a parsed line passes both filters. An empty filter passes.
[[nodiscard]] constexpr bool Matches(const Line& line, std::string_view message_filter,
    std::string_view level_filter)
{
    return ContainsIgnoringCase(line.message, message_filter)
        && ContainsIgnoringCase(line.level, level_filter);
}
}
