#pragma once

#include "utils/CSingleton.hpp"
#include "SettingsSchema.hpp"
#include "interface/IBasicGuiCustomization.hpp"
#include "interface/ISettingsBinding.hpp"

#include <functional>
#include <iosfwd>
#include <string_view>
#include <vector>

// !\brief Smallest main-frame size, and the default when none is remembered.
// These are settings defaults, so they live with the settings rather than in a
// GUI header - that include is what kept Settings bound to the GUI target.
constexpr int WINDOW_SIZE_X = 1024;
constexpr int WINDOW_SIZE_Y = 768;

class Logger;

// !\brief Which panels the main frame shows.
//
// This was a union over a nameless bitfield struct, which duplicated
// settings_schema::PageBit's bit order and relied on a test to keep the two
// in step. The nameless struct is also a Microsoft extension that /W4
// rejects; it only compiled because a boost header in the precompiled header
// happened to disable the warning. One mask, named accessors, one bit order.
class UsedPages
{
public:
    UsedPages() = default;
    explicit UsedPages(std::uint16_t mask) : pages(mask) {}

    [[nodiscard]] bool Has(settings_schema::PageBit bit) const
    {
        return (pages & static_cast<std::uint16_t>(bit)) != 0;
    }

    void Enable(settings_schema::PageBit bit)
    {
        pages |= static_cast<std::uint16_t>(bit);
    }

    /* Named for the panel each bit turns on. Every one is defined from the
       schema, so there is no second bit order to keep in step. */
    [[nodiscard]] bool main() const          { return Has(settings_schema::Page_Main); }
    [[nodiscard]] bool escaper() const       { return Has(settings_schema::Page_StringEscaper); }
    [[nodiscard]] bool debug() const         { return Has(settings_schema::Page_Debug); }
    [[nodiscard]] bool file_browser() const  { return Has(settings_schema::Page_FileBrowser); }
    [[nodiscard]] bool cmd_executor() const  { return Has(settings_schema::Page_CmdExecutor); }
    [[nodiscard]] bool can() const           { return Has(settings_schema::Page_CanSender); }
    [[nodiscard]] bool did() const           { return Has(settings_schema::Page_Did); }
    [[nodiscard]] bool modbus_master() const { return Has(settings_schema::Page_ModbusMaster); }
    [[nodiscard]] bool alarm_panel() const   { return Has(settings_schema::Page_AlarmPanel); }
    [[nodiscard]] bool time_tracker() const  { return Has(settings_schema::Page_TimeTracker); }
    [[nodiscard]] bool log() const           { return Has(settings_schema::Page_Log); }
    [[nodiscard]] bool backup() const        { return Has(settings_schema::Page_Backup); }

    std::uint16_t pages = 0;
};

class Settings : public CSingleton < Settings >, public ISettingsBinding
{
    friend class CSingleton < Settings >;

public:
    Settings() = default;
    ~Settings() = default;

    // !\brief Initialize settings
    void Init();

    // !\brief Load application settings from settings.ini file
    void LoadFile();

    // !\brief Save application settings to settings.ini file
    // !\param write_default_macros [in] If settings.ini file doesn't exists - write a few macro lines there as an example 
    void SaveFile(bool write_default_macros);

    // !\brief Register one subsystem's settings block.
    //
    // Registration order is the order the blocks appear in settings.ini. The
    // composition root registers everything before Init() runs, so Settings
    // itself no longer needs to know which subsystems exist or what they store.
    void RegisterBinding(ISettingsBinding& binding);

    // !\brief Drop every registered binding.
    void ClearBindings();

    // !\brief Where the current window size comes from when saving.
    //
    // Settings used to read it straight off the wxWidgets top window, which is
    // the only reason this class needed a GUI at all. The composition root
    // supplies the accessor instead.
    using WindowSizeProvider = std::function<LogicalSize()>;
    void SetWindowSizeProvider(WindowSizeProvider provider);

    // ISettingsBinding - Settings owns the [App] block itself.
    [[nodiscard]] std::string_view SettingsSection() const override { return "App"; }
    void LoadSettings(SettingsReader& reader) override;
    void SaveSettings(std::ostream& out) const override;

    // !\brief The logger, whose two keys live in the [App] block this owns.
    // Supplied by the composition root, so reading settings no longer reaches
    // back into the logger singleton and vice versa.
    void SetLogger(Logger& logger) noexcept { m_Logger = &logger; }

    // !\brief Minimize application on exit
    bool minimize_on_exit = false;

    // !\brief Start application as minimized
    bool minimize_on_startup = false;

    // !\brief Used pages
    UsedPages used_pages = {};

    // !\brief Default start page for application
    uint8_t default_page = 0;

    // !\brief Remember application window size when resized
    bool remember_window_size = false;

    // !\brief Main frame size
    LogicalSize window_size{ WINDOW_SIZE_X, WINDOW_SIZE_Y };

    // !\brief Num lock always on status
    bool always_on_numlock = false;

    // !\bried Shared drive mapped letter
    char shared_drive_letter = 'Z';

    // !\brief Crypto prices (currently ETH & BTC) update interval from coinbase.com [seconds]
    // !\note 0 - disabled
    uint16_t crypto_price_update = 5;

    // The panel list is plain text handling with no dependency on the running
    // application, so it lives in settings_schema where it can be tested.
    static UsedPages ParseUsedPagesFromString(const std::string& in)
    {
        UsedPages pages;
        pages.pages = settings_schema::ParseUsedPages(in);
        return pages;
    }

    static std::string ParseUsedPagesToString(UsedPages& in)
    {
        return settings_schema::FormatUsedPages(in.pages);
    }

private:
    Logger* m_Logger = nullptr;
    std::vector<ISettingsBinding*> m_Bindings;
    WindowSizeProvider m_WindowSizeProvider;
};

