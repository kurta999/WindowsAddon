#pragma once

#include <optional>
#include <string>

// !\brief The system clipboard, as the text-replacing features need it.
//
// PathSeparator is plain string transformation wrapped around two clipboard
// calls. Naming those calls is what lets the transformation be built, and
// tested, without wxWidgets.
class IClipboard
{
public:
    virtual ~IClipboard() = default;

    // !\brief The clipboard's text, or nothing when it holds no text.
    [[nodiscard]] virtual std::optional<std::string> GetText() = 0;

    // !\brief Replace the clipboard's text. False when the clipboard refused.
    virtual bool SetText(const std::string& text) = 0;
};
