
#include "TcpMessageParser.hpp"

#include <algorithm>
#include <array>
#include <string>

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

bool IsSeparator(char value) noexcept
{
    return value == '\\' || value == '/';
}

// Rejects everything that could either escape the target drive or change how
// the resulting string is interpreted once it reaches the shell.
bool IsForbiddenPathCharacter(char value) noexcept
{
    const auto byte = static_cast<unsigned char>(value);
    if(byte < 0x20 || byte >= 0x7F)  /* control characters and non-ASCII */
        return true;

    switch(value)
    {
        case '"': case '<': case '>': case '|':
        case '*': case '?': case ':':
            return true;
        default:
            return false;
    }
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

std::optional<std::string> SanitizeExplorerPath(std::string_view path)
{
    /* Strip the trailing line ending the sender's `echo` leaves behind. */
    while(!path.empty() && (path.back() == '\r' || path.back() == '\n'))
        path.remove_suffix(1);

    if(path.empty() || path.size() > MaxExplorerPathLength)
        return std::nullopt;

    if(std::any_of(path.begin(), path.end(), IsForbiddenPathCharacter))
        return std::nullopt;

    /* A leading double separator is a UNC prefix and would leave the drive. */
    if(path.size() >= 2 && IsSeparator(path[0]) && IsSeparator(path[1]))
        return std::nullopt;

    std::string normalized;
    normalized.reserve(path.size() + 1);
    normalized.push_back('\\');

    std::size_t index = 0;
    while(index < path.size())
    {
        while(index < path.size() && IsSeparator(path[index]))
            ++index;

        const std::size_t start = index;
        while(index < path.size() && !IsSeparator(path[index]))
            ++index;

        const std::string_view component = path.substr(start, index - start);
        if(component.empty())
            continue;

        /* "." is a no-op; ".." would climb above the share root. */
        if(component == ".")
            continue;
        if(component == "..")
            return std::nullopt;

        /* Trailing dots and spaces are silently stripped by Win32, which makes
           two different requests resolve to the same object. Refuse instead. */
        if(component.back() == '.' || component.back() == ' ')
            return std::nullopt;

        if(normalized.size() > 1)
            normalized.push_back('\\');
        normalized.append(component);
    }

    if(normalized.size() <= 1)  /* nothing but separators */
        return std::nullopt;

    return normalized;
}
}
