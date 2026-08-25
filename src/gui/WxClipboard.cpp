#include "pch.hpp"

#include "WxClipboard.hpp"

std::optional<std::string> WxClipboard::GetText()
{
    if(!wxTheClipboard->Open())
        return std::nullopt;

    std::optional<std::string> text;
    if(wxTheClipboard->IsSupported(wxDF_TEXT))
    {
        wxTextDataObject data;
        wxTheClipboard->GetData(data);
        text = std::string(data.GetText().ToStdString());
    }
    wxTheClipboard->Close();
    return text;
}

namespace gui
{
bool CopyTextToClipboard(const wxString& text)
{
    if(!wxTheClipboard->Open())
        return false;

    const bool ok = wxTheClipboard->SetData(new wxTextDataObject(text));
    wxTheClipboard->Flush();
    wxTheClipboard->Close();
    return ok;
}
}

bool WxClipboard::SetText(const std::string& text)
{
    return gui::CopyTextToClipboard(wxString(text));
}
