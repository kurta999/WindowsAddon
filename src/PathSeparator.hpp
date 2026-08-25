#pragma once

#include <string>
#include "interface/ISettingsBinding.hpp"
#include <iosfwd>
#include <string_view>
#include "interface/IHotkeyHandler.hpp"
#include "interface/IClipboard.hpp"
#include "interface/INotificationSink.hpp"

// !\brief Rewrites the path on the clipboard, on one global hotkey.
//
// Its outbound edges - the clipboard and the notification sink - were already
// handed in by the composition root; this used to be the last thing about it
// that was global, and only two collaborators ever looked it up.
class PathSeparator : public ISettingsBinding, public IHotkeyHandler
{
public:
    // IHotkeyHandler - this feature owns one global key.
    [[nodiscard]] std::string HotkeyBinding() const override { return replace_key; }
    [[nodiscard]] std::string_view HotkeyOwner() const override { return "PathSeparator"; }
    void OnHotkeyPressed() override;
    [[nodiscard]] bool HotkeyNeedsUiThread() const override { return true; }

    // ISettingsBinding - this subsystem owns its own block of settings.ini.
    [[nodiscard]] std::string_view SettingsSection() const override { return "PathSeparator"; }
    void LoadSettings(SettingsReader& reader) override;
    void SaveSettings(std::ostream& out) const override;

    PathSeparator() = default;

    enum class ReplaceType
    {
        PATH_SEPARATOR,
        WSL
    };

    // !\brief Replace path separators to opposite ones in clipboard 
    void ReplaceClipboard(ReplaceType type);

    // !\brief The clipboard this works on, and where the result is announced.
    // Supplied by the composition root; without them the feature is inert
    // rather than reaching for a window that may not be there.
    void SetClipboard(IClipboard* clipboard) noexcept { m_Clipboard = clipboard; }
    void SetNotificationSink(INotificationSink* sink) noexcept { m_Sink = sink; }

    // !\brief The transformation itself, with no clipboard involved.
    [[nodiscard]] static std::string Replace(std::string input, ReplaceType type);

    // !\brief Path separator execution key
    std::string replace_key = "F11";

private:
    IClipboard* m_Clipboard = nullptr;
    INotificationSink* m_Sink = nullptr;

    // !\brief Replace path separators to opposite ones in given string
    // !\param str [in] Reference to string where separators will be replaced
    static void ReplaceString(std::string& str);    
    
    // !\brief Convert a WSL path (/mnt/c/...) to a Windows path (C:\...)
    // !\param str [in] Reference to string where the path will be converted
    static void ReplaceStringFromWindowsToWsl(std::string& str);
};