#include "SensorDataParser.hpp"

#include <regex>
#include <string_view>

namespace
{
void EraseAll(std::string& value, std::string_view token)
{
    for(size_t pos = value.find(token); pos != std::string::npos; pos = value.find(token, pos))
        value.erase(pos, token.size());
}

void ReplaceAll(std::string& value, std::string_view from, std::string_view to)
{
    for(size_t pos = value.find(from); pos != std::string::npos; pos = value.find(from, pos + to.size()))
        value.replace(pos, from.size(), to);
}
}

namespace SensorDataParser
{

std::optional<std::vector<std::string>> Parse(const char* data, size_t len)
{
    if(data == nullptr || len == 0)
        return std::nullopt;

    std::string s(data, len);
    EraseAll(s, "MEAS_DATA");
    EraseAll(s, "SCD30");
    EraseAll(s, "CO");
    EraseAll(s, "BME680");
    EraseAll(s, "HONEYWELL");
    EraseAll(s, "VEML6070");
    EraseAll(s, "TCS");
    ReplaceAll(s, "nan", "0.0");

    const std::regex num_regex(R"([-+]?(\d+([.]\d*)?|[.]\d+)([eE][-+]?\d+)?)");
    auto begin = std::sregex_iterator(s.begin(), s.end(), num_regex);

    std::vector<std::string> result;
    for(auto it = begin; it != std::sregex_iterator(); ++it)
        result.push_back((*it).str());

    if(result.empty())
        return std::nullopt;

    return result;
}

}
