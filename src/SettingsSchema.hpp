#pragma once

// The part of the settings layer that does not depend on the application.
//
// Settings::LoadFile is a long procedure that pushes values into two dozen
// subsystems, so it can only run inside a fully wired application. What it
// insists on - which sections and keys have to be there, and how the panel list
// is spelled - is data, and lives here instead. That makes it something the
// dependency-free test suite can check, including against the settings.ini this
// project ships as an example.

#include <cstdint>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace settings_schema
{
// !\brief One panel of the main frame, as [App] UsedPages names it.
//
// This is the one place the panel bit order is declared. UsedPages in
// Settings.hpp reads these bits through named accessors rather than
// restating the order as a bitfield.
enum PageBit : std::uint16_t
{
    Page_Main = 1u << 0,
    Page_StringEscaper = 1u << 1,
    Page_Debug = 1u << 2,
    Page_FileBrowser = 1u << 3,
    Page_CmdExecutor = 1u << 4,
    Page_CanSender = 1u << 5,
    Page_Did = 1u << 6,
    Page_ModbusMaster = 1u << 7,
    Page_AlarmPanel = 1u << 8,
    Page_TimeTracker = 1u << 9,
    Page_Log = 1u << 10,
    Page_Backup = 1u << 11,

    Page_All = 0x0FFF,
};

// !\brief One setting, named the way it appears in settings.ini.
struct SettingName
{
    std::string_view section;
    std::string_view key;

    bool operator==(const SettingName&) const = default;
};

// !\brief Every setting Settings::LoadFile insists on. A missing one aborts the
// load and leaves everything after it at its default, so a settings.ini that
// does not define all of these is broken.
[[nodiscard]] const std::vector<SettingName>& RequiredSettings();

// !\brief Settings that are only read under some condition: a section that may
// be absent, a numbered section that may not exist, or a switch that turns them
// on. Declared so the same table covers everything LoadFile reads.
[[nodiscard]] const std::vector<SettingName>& ConditionalSettings();

// !\brief Whether the schema declares this setting at all, required or not.
// Numbered sections ("Keys_Macro3", "Backup_2") match their family.
[[nodiscard]] bool IsDeclared(std::string_view section, std::string_view key);

// !\brief The section/key pairs an ini file defines, in file order.
// Comments, blank lines and keys outside any section are skipped.
[[nodiscard]] std::vector<std::pair<std::string, std::string>> ReadSettingNames(
    std::string_view ini_content);

// !\brief The required settings this ini content does not define.
[[nodiscard]] std::vector<SettingName> MissingRequiredSettings(std::string_view ini_content);

// !\brief Reads the [App] UsedPages list into a PageBit mask. Names are matched
// case-insensitively and in any order, which is how the setting has always
// behaved.
[[nodiscard]] std::uint16_t ParseUsedPages(std::string_view in);

// !\brief Writes a PageBit mask back out as the [App] UsedPages list.
[[nodiscard]] std::string FormatUsedPages(std::uint16_t pages);
}
