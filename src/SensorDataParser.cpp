#include "pch.hpp"

namespace SensorDataParser
{

std::optional<std::vector<std::string>> Parse(const char* data, size_t len)
{
    std::string s(data, data + len);
    boost::algorithm::erase_all(s, "MEAS_DATA");
    boost::algorithm::erase_all(s, "SCD30");
    boost::algorithm::erase_all(s, "CO");
    boost::algorithm::erase_all(s, "BME680");
    boost::algorithm::erase_all(s, "HONEYWELL");
    boost::algorithm::erase_all(s, "VEML6070");
    boost::algorithm::erase_all(s, "TCS");
    boost::algorithm::replace_all(s, "nan", "0.0");

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
