#pragma once

#include <optional>
#include <string>
#include <vector>

namespace SensorDataParser
{
    // Parses a raw sensor broadcast string into an ordered vector of numeric tokens.
    // Returns std::nullopt if the string is malformed or has too few fields.
    std::optional<std::vector<std::string>> Parse(const char* data, size_t len);
}
