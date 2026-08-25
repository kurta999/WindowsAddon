#include "pch_core.hpp"
#include "SerialPort.hpp"
#include "Logger.hpp"
#include "Utils.hpp"
#include "SettingsReader.hpp"
#include "SettingsWriter.hpp"
#include <ostream>
#include "interface/IKeySink.hpp"

using namespace std::chrono_literals;

constexpr auto SERIAL_PORT_TIMEOUT = 1000ms;
constexpr auto SERIAL_PORT_EXCEPTION_TIMEOUT = 1000ms;

SerialPort::~SerialPort()
{

}

void SerialPort::Init()
{
    if(is_enabled)
    {
        /* The transport's callback carries a size_t length; std::bind would
           narrow it silently, so the conversion is spelled out here. */
        auto recv_f = [this](const char* data, std::size_t len) { OnDataReceived(data, static_cast<unsigned int>(len)); };
        InitInternal("SerialPort", SERIAL_PORT_TIMEOUT, SERIAL_PORT_EXCEPTION_TIMEOUT, recv_f, nullptr);
    }
    else
    {
        DeInitInternal();
    }
}

void SerialPort::SetForwardToTcp(bool enable)
{
    forward_serial_to_tcp = enable;
}

bool SerialPort::IsForwardToTcp() const
{
    return forward_serial_to_tcp;
}

void SerialPort::SetRemoteTcpIp(const std::string& ip)
{
    remote_tcp_ip = ip;
}

const std::string& SerialPort::GetRemoteTcpIp() const
{
    return remote_tcp_ip;
}

void SerialPort::SetRemoteTcpPort(uint16_t remote_port)
{
    remote_tcp_port = remote_port;
}

uint16_t SerialPort::GetRemoteTcpPort() const
{
    return remote_tcp_port;
}

void SerialPort::SimulateDataReception(const char* data, unsigned int len)
{
    OnDataReceived(data, len);
}

void SerialPort::OnDataReceived(const char* data, unsigned int len)
{
    if(forward_serial_to_tcp && IsUsingVmOrWsl())
    {
        utils::SendTcpBlocking(remote_tcp_ip, remote_tcp_port, data, len);
        return;
    }

    if(m_KeySink != nullptr)
        m_KeySink->OnKeypadData(std::string_view(data, len));
}

// \x00\x00\x00\x00\x00\x00\x00\x00\x00\x54\x00\x00\x00\x00\x00\x4C\x45
// \h(00 00 00 00 00 00 00 00 00 54 00 00 00 00 00 4C 45)

bool SerialPort::IsUsingVmOrWsl()
{
#ifdef _WIN32
    bool ret = false;
    char window_title[256];
    HWND foreground = GetForegroundWindow();
    GetWindowTextA(foreground, window_title, sizeof(window_title));
    if(boost::algorithm::contains(window_title, "Oracle VM VirtualBox") || boost::algorithm::contains(window_title, "(Ubuntu)"))
        ret = true;
    return ret;
#else
    return false;
#endif
}

void SerialPort::LoadSettings(SettingsReader& reader)
{
    SetEnabled(utils::stob(reader.Required("COM_Backend", "Enable")));
    SetComPort(utils::stoi<uint16_t>(reader.Required("COM_Backend", "COM")));
    SetForwardToTcp(utils::stob(reader.Required("COM_Backend", "ForwardViaTcp")));
    SetRemoteTcpIp(reader.Required("COM_Backend", "RemoteTcpIp"));
    SetRemoteTcpPort(utils::stoi<uint16_t>(reader.Required("COM_Backend", "RemoteTcpPort")));
}

void SerialPort::SaveSettings(std::ostream& out) const
{
    SettingsWriter(out, "COM_Backend")
        .Key("Enable", IsEnabled())
        .Key("COM", GetComPort(), "Com port for UART where the data is received from STM32")
        .Key("ForwardViaTcp", IsForwardToTcp(), "Is data have to be forwarded to remote TCP server")
        .Key("RemoteTcpIp", GetRemoteTcpIp())
        .Key("RemoteTcpPort", GetRemoteTcpPort())
        .Blank();
}
