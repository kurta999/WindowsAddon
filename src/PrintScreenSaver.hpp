#pragma once

#include "utils/CSingleton.hpp"

#include <filesystem>
#include <cstring>
#ifdef _WIN32
#include <Windows.h>
#endif
#include <future>
#include "interface/ISettingsBinding.hpp"
#include <iosfwd>
#include <string_view>
#include "interface/IHotkeyHandler.hpp"
#include "interface/INotificationSink.hpp"

// !\brief Saves a screenshot to a folder, on one global hotkey.
class PrintScreenSaver : public ISettingsBinding, public IHotkeyHandler
{
public:
    // IHotkeyHandler - this feature owns one global key.
    [[nodiscard]] std::string HotkeyBinding() const override { return screenshot_key; }
    [[nodiscard]] std::string_view HotkeyOwner() const override { return "PrintScreenSaver"; }
    void OnHotkeyPressed() override;

    // !\brief Where "screenshot saved" and "screenshot failed" go.
    void SetNotificationSink(INotificationSink* sink) noexcept { m_Sink = sink; }
    [[nodiscard]] bool HotkeyNeedsUiThread() const override { return true; }

    // ISettingsBinding - this subsystem owns its own block of settings.ini.
    [[nodiscard]] std::string_view SettingsSection() const override { return "Screenshot"; }
    void LoadSettings(SettingsReader& reader) override;
    void SaveSettings(std::ostream& out) const override;

    // !\brief Initialize function
    void Init();

    // !\brief Start screenshot saving
    void SaveScreenshot();

    // !\brief Screenshot timestamp format in filename
    std::string timestamp_format = "%Y.%m.%d %H.%M.%S";

    // !\brief Screenshots path (relative to application directory)
    std::filesystem::path screenshot_path = "Screenshots";

    // !\brief Screenshot key
    std::string screenshot_key = "F12";

private:
    INotificationSink* m_Sink = nullptr;

    // !\brief Format screenshot filename timestamp
    void FormatTimestamp(char* buf, size_t len);

    // !\brief Screenshot saving logic
    void DoSave();

#ifdef _WIN32
    // !\brief Returns byte offset from start of BITMAPINFO to pixel data, for a packed DIB
    INT GetPixelDataOffsetForPackedDIB(const BITMAPINFOHEADER* BitmapInfoHeader);

    // !\brief Decode a BMP byte buffer into a raw RGBA image; returns 0 on success
    unsigned decodeBMP(std::vector<unsigned char>& image, unsigned& w, unsigned& h, const std::vector<unsigned char>& bmp);
#endif

    // !\brief Future for screenshot saving
    std::future<void> screenshot_future;
};