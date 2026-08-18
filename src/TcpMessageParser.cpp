#include "TcpMessageParser.hpp"

#include <algorithm>
#include <array>

namespace
{
struct GraphRoute
{
    std::string_view request;
    std::string_view file;
};

constexpr std::array GraphRoutes{
    GraphRoute{"GET /graphs", "Temperature.html"},
    GraphRoute{"GET /Temperature", "Temperature.html"},
    GraphRoute{"GET /Humidity", "Humidity.html"},
    GraphRoute{"GET /CO2", "CO2.html"},
    GraphRoute{"GET /VOC", "VOC.html"},
    GraphRoute{"GET /CO", "CO.html"},
    GraphRoute{"GET /PM25", "PM25.html"},
    GraphRoute{"GET /PM10", "PM10.html"},
    GraphRoute{"GET /Pressure", "Pressure.html"},
    GraphRoute{"GET /R", "R.html"},
    GraphRoute{"GET /G", "G.html"},
    GraphRoute{"GET /B", "B.html"},
    GraphRoute{"GET /Lux", "Lux.html"},
    GraphRoute{"GET /CCT", "CCT.html"},
    GraphRoute{"GET /UV", "UV.html"},
    GraphRoute{"GET /Chart.min.js.download", "Chart.min.js.download"},
    GraphRoute{"GET /utils.js.download", "utils.js.download"},
};

bool MatchesHttpRoute(std::string_view message, std::string_view route) noexcept
{
    if(!message.starts_with(route))
        return false;

    if(message.size() == route.size())
        return true;

    const char delimiter = message[route.size()];
    return delimiter == ' ' || delimiter == '?' || delimiter == '\r' || delimiter == '\n';
}
}

namespace tcp_message
{
std::span<char> BoundedMessage(std::span<char> buffer,
                               std::size_t transferred_bytes) noexcept
{
    return buffer.first(std::min(buffer.size(), transferred_bytes));
}

std::optional<ParsedMessage> Parse(std::string_view message) noexcept
{
    constexpr std::string_view MeasurementsPrefix = "MEAS_DATA";
    constexpr std::string_view ExplorerPrefix = "expw";

    if(message.starts_with(MeasurementsPrefix))
        return ParsedMessage{Command::Measurements, message};

    if(message.starts_with(ExplorerPrefix))
        return ParsedMessage{Command::OpenExplorer, message.substr(ExplorerPrefix.size())};

    for(const auto& route : GraphRoutes)
    {
        if(MatchesHttpRoute(message, route.request))
            return ParsedMessage{Command::Graph, route.file};
    }

    return std::nullopt;
}
}
