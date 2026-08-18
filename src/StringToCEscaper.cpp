#include "StringToCEscaper.hpp"

#include <string_view>

namespace
{
void ReplaceAll(std::string& value, std::string_view from, std::string_view to)
{
    for(size_t pos = value.find(from); pos != std::string::npos; pos = value.find(from, pos + to.size()))
        value.replace(pos, from.size(), to);
}
}

void StringEscaper::EscapeString(std::string& input, bool escape_percent, bool insert_backslash_to_end)
{
    ReplaceAll(input, "\\", "\\\\");
    ReplaceAll(input, "\"", "\\\"");
    if(escape_percent)
        ReplaceAll(input, "%", "%%");
    if(insert_backslash_to_end)
        ReplaceAll(input, "\r\n", "\\\r\n");
}
