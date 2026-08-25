#include "pch.hpp"

#include "Prompts.hpp"

namespace gui
{
std::optional<uint8_t> PromptForByte(wxWindow* parent, const wxString& message,
    const wxString& title, uint8_t current, std::string_view what)
{
    wxTextEntryDialog dialog(parent, message, title);
    dialog.SetValue(std::to_string(current));

    if(dialog.ShowModal() != wxID_OK)
        return std::nullopt;

    const std::string typed = dialog.GetValue().ToStdString();
    const auto value = utils::TryParse<uint8_t>(typed);
    if(!value)
    {
        LOG(LogLevel::Warning, "Ignoring {} '{}': not a number in 0-255", what, typed);
        return std::nullopt;
    }

    return value;
}
}
