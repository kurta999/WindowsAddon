#pragma once

#include "interface/IClipboard.hpp"

namespace gui
{
// !\brief Puts `text` on the system clipboard. False when it is busy.
//
// Seven spellings of open -> SetData(new wxTextDataObject(...)) -> close
// existed: three in the escaper panel, one each in the debug panel, the two log
// panels and the grid row copy, and WxClipboard::SetText below.
//
// The two log panels also called Flush(), which hands the data to the system so
// it outlives this process; the others did not, so a string copied out of the
// escaper was gone the moment the application closed. Everything flushes now.
bool CopyTextToClipboard(const wxString& text);
}

// !\brief The wxWidgets clipboard adapter.
//
// Lives in the GUI target because wxTheClipboard does; everything that only
// wants "read text, write text" depends on IClipboard instead.
class WxClipboard : public IClipboard
{
public:
    [[nodiscard]] std::optional<std::string> GetText() override;
    bool SetText(const std::string& text) override;
};
