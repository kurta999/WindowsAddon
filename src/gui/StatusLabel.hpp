#pragma once

#include <string_view>

#include <wx/stattext.h>

#include "SerialPortConnectionStatus.hpp"

// !\brief The one place a connection status becomes a coloured label.
//
// The main panel carried the same twelve lines four times over - TCP, keyboard,
// CAN, Modbus - differing only in the name printed and which port was asked
// whether it was healthy. Copies that close do drift: the Modbus row tested the
// CAN port's IsOk() for long enough to ship, which is the defect this exists to
// make unrepeatable. The Modbus data panel spelled a fifth variant with its own
// vocabulary and its own colours.
namespace gui
{
// !\brief What a transport row reports.
enum class LinkStatus : unsigned char
{
    // !\brief Switched off in settings - nothing is trying to connect.
    Off,

    // !\brief Enabled, but the link is not up.
    Disconnected,

    Connecting,
    Ok,
    Error,
};

// !\brief The vocabulary a row is written in.
//
// The status strip has room for three characters and says OK/ERR/OFF; the
// Modbus panel has a line to itself and spells the state out. Same states and
// the same colours, different words.
enum class LinkWording : unsigned char
{
    Terse,
    Verbose,
};

// !\brief The status of anything answering IsEnabled() and IsOk().
//
// A template rather than an interface because the four ports it is called with
// share these two methods without sharing a base - and adding one to them for
// the sake of a label would be the wrong direction of dependency.
template <typename Port>
[[nodiscard]] LinkStatus LinkStatusOf(const Port& port)
{
    if(!port.IsEnabled())
        return LinkStatus::Off;

    return port.IsOk() ? LinkStatus::Ok : LinkStatus::Error;
}

// !\brief The status a serial port's connection state stands for.
[[nodiscard]] LinkStatus LinkStatusOf(SerialPortConnectionState state);

// !\brief Writes "<name>: <word>" onto `label` and colours it by status.
void SetLinkStatus(wxStaticText* label, std::string_view name, LinkStatus status,
    LinkWording wording = LinkWording::Terse);
}
