#pragma once

#include <cstdint>
#include <optional>
#include <string_view>

#include <wx/string.h>
#include <wx/window.h>

// !\brief The small modal prompts panels share.
namespace gui
{
// !\brief Asks for a number in 0-255, seeded with `current`.
//
// Returns nothing when the user cancels or types something that is not a
// number in range; the caller leaves its setting alone in both cases. Three
// panels - the CAN sender's log level and favourite level, the Modbus data
// panel's favourite level - wrote this dialog out, comment included, and all
// three had the same defect before Phase 0: the failed parse left the variable
// at zero and stored it anyway, so a typo silently reset the default.
//
// `what` names the value in the warning log, which is the only part of the
// three copies that genuinely differed.
[[nodiscard]] std::optional<uint8_t> PromptForByte(wxWindow* parent, const wxString& message,
    const wxString& title, uint8_t current, std::string_view what);
}
