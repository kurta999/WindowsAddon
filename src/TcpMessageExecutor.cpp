#include "pch.hpp"

constexpr std::string_view TCP_HTTP_HEADER =
    "HTTP/1.1 200 OK\r\n Content-type:text/html\r\n Connection: close\r\n\r\n";

TcpMessageExecutor::TcpMessageExecutor() = default;

TcpMessageReturn TcpMessageExecutor::HandleAirQualityData(const SharedSession& session,
                                                           std::span<const char> message)
{
    Sensors::Get()->HandleAndForwardIncomingMeasurements(
        message.data(), message.size(), session->sessionAddress.c_str());
    return std::make_tuple(true, true, "");
}

TcpMessageReturn TcpMessageExecutor::HandleOpenExplorer(std::string_view path)
{
#ifdef _WIN32
    std::string windows_path(path);
    std::replace(windows_path.begin(), windows_path.end(), '/', '\\');
    const std::wstring params(windows_path.begin(), windows_path.end());

    const std::string shared_drive_letter(1, Settings::Get()->shared_drive_letter);
    const std::wstring drive(shared_drive_letter.begin(), shared_drive_letter.end());
    const std::wstring command_line = drive + L":" + params;
    ShellExecuteW(nullptr, L"open", L"explorer.exe", command_line.c_str(), nullptr, SW_NORMAL);
    LOG(LogLevel::Normal, L"Explorer open recv: {}", command_line);
#else
    (void)path;
#endif
    return std::make_tuple(true, true, "");
}

TcpMessageReturn TcpMessageExecutor::HandleGraphs(std::string_view file_on_disk)
{
    std::string to_send(TCP_HTTP_HEADER);
    std::ifstream input(std::string("Graphs/") + std::string(file_on_disk), std::ifstream::binary);
    if(input)
    {
        input.seekg(0, std::ios::end);
        const auto size = static_cast<std::size_t>(input.tellg());
        input.seekg(0);
        to_send.resize(TCP_HTTP_HEADER.size() + size);
        input.read(to_send.data() + TCP_HTTP_HEADER.size(), static_cast<std::streamsize>(size));
    }
    else
    {
        LOG(LogLevel::Error, "Failed to open {}", file_on_disk);
    }
    return std::make_tuple(true, true, std::move(to_send));
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
