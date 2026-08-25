#include "pch.hpp"

#include "StatusLabel.hpp"

namespace gui
{
namespace
{
// !\brief The word each status is written with, and the colour it is painted.
//
// wxGREEN and friends are pointers wxWidgets fills in during initialisation, so
// this table is built per call rather than held as a static.
struct StatusStyle
{
    const char* terse;
    const char* verbose;
    wxColour colour;
};

StatusStyle StyleOf(LinkStatus status)
{
    switch(status)
    {
        case LinkStatus::Ok:
            return { "OK", "Connected", *wxGREEN };
        case LinkStatus::Connecting:
            return { "...", "Connecting", wxColour(220, 140, 0) };
        case LinkStatus::Error:
            return { "ERR", "Error", *wxRED };
        case LinkStatus::Disconnected:
            return { "---", "Disconnected", *wxBLACK };
        case LinkStatus::Off:
            break;
    }
    return { "OFF", "Off", *wxBLUE };
}
}

LinkStatus LinkStatusOf(SerialPortConnectionState state)
{
    switch(state)
    {
        case SerialPortConnectionState::Connected:
            return LinkStatus::Ok;
        case SerialPortConnectionState::Connecting:
            return LinkStatus::Connecting;
        case SerialPortConnectionState::Error:
            return LinkStatus::Error;
        case SerialPortConnectionState::Disconnected:
            break;
    }
    return LinkStatus::Disconnected;
}

void SetLinkStatus(wxStaticText* label, std::string_view name, LinkStatus status, LinkWording wording)
{
    if(!label)
        return;

    const StatusStyle style = StyleOf(status);
    const char* word = wording == LinkWording::Terse ? style.terse : style.verbose;

    label->SetLabelText(wxString::Format("%s: %s", wxString(name.data(), name.size()), word));
    label->SetForegroundColour(style.colour);
}
}
