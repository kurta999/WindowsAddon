#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

namespace SensorDataParser
{
    // Parses a raw sensor broadcast string into an ordered vector of numeric tokens.
    // Returns std::nullopt if the input is null, empty, or contains no numbers.
    std::optional<std::vector<std::string>> Parse(const char* data, size_t len);
}
