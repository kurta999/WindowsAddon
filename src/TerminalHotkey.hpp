#pragma once

#include "utils/CSingleton.hpp"
#include <string>
#include <chrono>
#include "interface/ISettingsBinding.hpp"
#include <functional>
#include <iosfwd>
#include <string_view>

typedef enum : uint8_t
{
    WINDOWS_TERMINAL,
    COMMAND_LINE,
    POWER_SHELL,
    BASH_TERMINAL,
} TerminalType;

class TerminalHotkey : public CSingleton < TerminalHotkey >, public ISettingsBinding
{
    friend class CSingleton < TerminalHotkey >;

public:
    // ISettingsBinding - this subsystem owns its own block of settings.ini.
    [[nodiscard]] std::string_view SettingsSection() const override { return "TerminalHotkey"; }
    void LoadSettings(SettingsReader& reader) override;
    void SaveSettings(std::ostream& out) const override;

    TerminalHotkey() = default;

    // !\brief Is enabled?
    bool is_enabled = true;

    // !\brief Type of terminal which one to open
    TerminalType type{TerminalType::WINDOWS_TERMINAL};

    // !\brief Set key
    // !\param key_str [in] Key as string
    void SetKey(const std::string& key_str);

    // !\brief Return key as string
    std::string GetKey() const;

    // !\brief Updates hotkey registration in main frame
    void UpdateHotkeyRegistration();

    // !\brief How the system-wide hotkey is (re-)registered.
    // Needs a window handle, which only the GUI owns, so it is handed in.
    using HotkeyRegistrar = std::function<void(int)>;
    void SetHotkeyRegistrar(HotkeyRegistrar registrar) { m_Registrar = std::move(registrar); }

    // !\brief Process function
    void Process();

private:
    HotkeyRegistrar m_Registrar;

    // !\brief VK key code for trigger key
#ifdef _WIN32
    int vkey = VK_F7;
#else
    int vkey = 0;
#endif
    // !\brief Open terminal with given path
    // !\param path [in] Path where to open the terminal
    void OpenTerminal(std::wstring& path);
};