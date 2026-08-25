#include "pch_core.hpp"
#include "TcpMessageExecutor.hpp"
#include "Logger.hpp"
#include "Utils.hpp"
#include "interface/IMeasurementSink.hpp"
#include "Session.hpp"
#include "Settings.hpp"

namespace
{
// Well-formed status line and headers: no leading spaces before the field
// names, and an explicit length so the response does not rely on the peer
// treating connection close as the terminator.
std::string BuildHttpResponse(std::string_view status, std::string_view content_type,
                              std::string_view body)
{
    std::string response;
    response.reserve(body.size() + 128);
    response.append("HTTP/1.1 ").append(status).append("\r\n");
    response.append("Content-Type: ").append(content_type).append("\r\n");
    response.append("Content-Length: ").append(std::to_string(body.size())).append("\r\n");
    response.append("Connection: close\r\n\r\n");
    response.append(body);
    return response;
}

std::string_view ContentTypeFor(std::string_view file_name)
{
    if(file_name.ends_with(".js") || file_name.ends_with(".js.download"))
        return "application/javascript; charset=utf-8";
    return "text/html; charset=utf-8";
}
}

TcpMessageExecutor::TcpMessageExecutor(IMeasurementSink& measurements,
    std::function<char()> shared_drive_letter) :
    m_Measurements(measurements), m_SharedDriveLetter(std::move(shared_drive_letter))
{
}

TcpMessageReturn TcpMessageExecutor::HandleAirQualityData(const SharedSession& session,
                                                           std::span<const char> message)
{
    m_Measurements.HandleIncomingMeasurements(
        message.data(), message.size(), session->sessionAddress.c_str());
    return std::make_tuple(true, true, "");
}

TcpMessageReturn TcpMessageExecutor::HandleOpenExplorer(std::string_view path)
{
#ifdef _WIN32
    /* This request arrives unauthenticated over the network, so the path is
       validated before it is allowed anywhere near the shell. */
    const auto sanitized = tcp_message::SanitizeExplorerPath(path);
    if(!sanitized)
    {
        LOG(LogLevel::Warning, "Rejected malformed Explorer path from network ({} bytes)", path.size());
        return std::make_tuple(true, true, "");
    }

    const char drive_letter = m_SharedDriveLetter ? m_SharedDriveLetter() : '\0';
    if(std::isalpha(static_cast<unsigned char>(drive_letter)) == 0)
    {
        LOG(LogLevel::Error, "SharedDriveLetter '{}' is not a drive letter, ignoring Explorer request", drive_letter);
        return std::make_tuple(true, true, "");
    }

    const std::string full_path = std::string(1, drive_letter) + ":" + *sanitized;

    /* Open directories only. Handing a file to the shell would let a remote
       peer launch anything reachable on the share. */
    std::error_code error;
    if(!std::filesystem::is_directory(full_path, error))
    {
        LOG(LogLevel::Warning, "Explorer request target is not an existing directory: {}", full_path);
        return std::make_tuple(true, true, "");
    }

    /* SanitizeExplorerPath guarantees pure ASCII, so widening byte-wise is exact. */
    const std::wstring wide_path(full_path.begin(), full_path.end());
    ShellExecuteW(nullptr, L"open", wide_path.c_str(), nullptr, nullptr, SW_NORMAL);
    LOG(LogLevel::Normal, "Explorer open recv: {}", full_path);
#else
    (void)path;
#endif
    return std::make_tuple(true, true, "");
}

TcpMessageReturn TcpMessageExecutor::HandleGraphs(std::string_view file_on_disk)
{
    std::ifstream input(std::string("Graphs/") + std::string(file_on_disk), std::ifstream::binary);
    if(!input)
    {
        LOG(LogLevel::Error, "Failed to open {}", file_on_disk);
        return std::make_tuple(true, true,
            BuildHttpResponse("404 Not Found", "text/plain; charset=utf-8", "Graph not available\r\n"));
    }

    std::string body;
    input.seekg(0, std::ios::end);
    const auto size = static_cast<std::size_t>(input.tellg());
    input.seekg(0);
    body.resize(size);
    input.read(body.data(), static_cast<std::streamsize>(size));
    body.resize(static_cast<std::size_t>(input.gcount()));

    if(body.empty())
        LOG(LogLevel::Warning, "Graphs/{} is empty - has the graph writer run yet?", file_on_disk);

    return std::make_tuple(true, true,
        BuildHttpResponse("200 OK", ContentTypeFor(file_on_disk), body));
}

TcpMessageReturn TcpMessageExecutor::Process(const SharedSession& session, std::span<char> message)
{
    if(!session)
        return std::make_tuple(false, false, "");

    const std::string_view request = message.empty()
        ? std::string_view{}
        : std::string_view(message.data(), message.size());
    const auto parsed = tcp_message::Parse(request);
    if(!parsed)
        return std::make_tuple(true, false, "");

    switch(parsed->command)
    {
        case tcp_message::Command::Measurements:
            return HandleAirQualityData(session, message);
        case tcp_message::Command::OpenExplorer:
            return HandleOpenExplorer(parsed->argument);
        case tcp_message::Command::Graph:
            return HandleGraphs(parsed->argument);
    }

    return std::make_tuple(true, false, "");
}
