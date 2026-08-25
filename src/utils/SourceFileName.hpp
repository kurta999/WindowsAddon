#pragma once

#include <string_view>

namespace utils
{
// !\brief The bare file name inside a __FILE__ style path.
//
// std::source_location::file_name() hands back an absolute path, and the log
// line wants only the last component. The logger computed that with a reverse
// scan for a separator, then handled the no-separator case thirty lines later
// as a null check on the result of the scan - so the two halves of one answer
// sat at opposite ends of a seventy-line function.
//
// Always narrow: file_name() is char whatever character type the message uses.
[[nodiscard]] constexpr std::string_view BareFileName(std::string_view path)
{
    const auto separator = path.find_last_of("/\\");
    return separator == std::string_view::npos ? path : path.substr(separator + 1);
}
}
