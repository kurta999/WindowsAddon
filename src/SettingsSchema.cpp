#include "SettingsSchema.hpp"

#include <algorithm>
#include <array>
#include <cctype>

namespace
{
struct PageName
{
    std::string_view name;
    std::uint16_t bit;
};

// The panel bit order; UsedPages reads these through named accessors.
constexpr std::array PAGE_NAMES{
    PageName{ "Main", settings_schema::Page_Main },
    PageName{ "StringEscaper", settings_schema::Page_StringEscaper },
    PageName{ "Debug", settings_schema::Page_Debug },
    PageName{ "FileBrowser", settings_schema::Page_FileBrowser },
    PageName{ "CmdExecutor", settings_schema::Page_CmdExecutor },
    PageName{ "CanSender", settings_schema::Page_CanSender },
    PageName{ "Did", settings_schema::Page_Did },
    PageName{ "ModbusMaster", settings_schema::Page_ModbusMaster },
    PageName{ "AlarmPanel", settings_schema::Page_AlarmPanel },
    PageName{ "TimeTracker", settings_schema::Page_TimeTracker },
    PageName{ "Log", settings_schema::Page_Log },
    PageName{ "Backup", settings_schema::Page_Backup },
};

[[nodiscard]] char Lower(char value)
{
    return static_cast<char>(std::tolower(static_cast<unsigned char>(value)));
}

// !\brief Case-insensitive substring search, matching what the setting used to
// be read with.
[[nodiscard]] bool IContains(std::string_view haystack, std::string_view needle)
{
    if(needle.empty())
        return true;
    if(haystack.size() < needle.size())
        return false;

    for(std::size_t start = 0; start + needle.size() <= haystack.size(); ++start)
    {
        std::size_t offset = 0;
        while(offset < needle.size() && Lower(haystack[start + offset]) == Lower(needle[offset]))
            ++offset;
        if(offset == needle.size())
            return true;
    }
    return false;
}

[[nodiscard]] std::string_view Trim(std::string_view value)
{
    while(!value.empty() && std::isspace(static_cast<unsigned char>(value.front())))
        value.remove_prefix(1);
    while(!value.empty() && std::isspace(static_cast<unsigned char>(value.back())))
        value.remove_suffix(1);
    return value;
}

// !\brief Whether `section` belongs to a numbered family such as "Keys_Macro3".
[[nodiscard]] bool MatchesFamily(std::string_view section, std::string_view family)
{
    if(!section.starts_with(family))
        return false;
    const std::string_view suffix = section.substr(family.size());
    return !suffix.empty() &&
        std::ranges::all_of(suffix, [](char c) { return std::isdigit(static_cast<unsigned char>(c)) != 0; });
}
}

namespace settings_schema
{
const std::vector<SettingName>& RequiredSettings()
{
    // Kept in the order LoadFile reads them, so a diff against that function
    // stays readable. Everything here is read with the reader's Required()
    // helper, which throws when the key is absent.
    static const std::vector<SettingName> settings{
        { "COM_Backend", "Enable" },
        { "COM_Backend", "COM" },
        { "COM_Backend", "ForwardViaTcp" },
        { "COM_Backend", "RemoteTcpIp" },
        { "COM_Backend", "RemoteTcpPort" },

        { "COM_TcpBackend", "Enable" },
        { "COM_TcpBackend", "ListeningIp" },
        { "COM_TcpBackend", "ListeningPort" },

        { "Sensors", "Enable" },
        { "Sensors", "TCP_Port" },
        { "Sensors", "GraphGenerationInterval" },
        { "Sensors", "GraphResolution" },
        { "Sensors", "IntegrationTime" },
        { "Sensors", "MeasurementForward" },
        { "Sensors", "MeasurementForward2" },

        { "CANSender", "Enable" },
        { "CANSender", "COM" },
        { "CANSender", "DeviceType" },
        { "CANSender", "AutoSend" },
        { "CANSender", "AutoRecord" },
        { "CANSender", "DefaultRecordingLogLevel" },
        { "CANSender", "DefaultFavouriteLevel" },
        { "CANSender", "DefaultEcuId" },
        { "CANSender", "DefaultTxList" },
        { "CANSender", "DefaultRxList" },
        { "CANSender", "DefaultMapping" },

        { "ModbusMaster", "Enable" },
        { "ModbusMaster", "ConnectionType" },
        { "ModbusMaster", "TcpIp" },
        { "ModbusMaster", "TcpPort" },
        { "ModbusMaster", "COM" },
        { "ModbusMaster", "PollingRate" },
        { "ModbusMaster", "ResponseTimeout" },
        { "ModbusMaster", "DefaultModbusConfig" },
        { "ModbusMaster", "AutoSend" },
        { "ModbusMaster", "AutoRecord" },
        { "ModbusMaster", "MaxRecordedEntries" },
        { "ModbusMaster", "Branch" },

        { "App", "MinimizeOnExit" },
        { "App", "MinimizeOnStartup" },
        { "App", "DefaultLogLevel" },
        { "App", "LogFilters" },
        { "App", "DefaultPage" },
        { "App", "RememberWindowSize" },
        { "App", "AlwaysOnNumLock" },
        { "App", "SharedDriveLetter" },
        { "App", "CryptoPriceUpdate" },
        { "App", "UsedPages" },

        { "CorsairHid", "Enable" },
        { "CorsairHid", "DebouncingInterval" },

        { "Screenshot", "ScreenshotKey" },
        { "Screenshot", "ScreenshotDateFormat" },
        { "Screenshot", "ScreenshotPath" },

        { "PathSeparator", "ReplacePathSeparatorKey" },

        { "TerminalHotkey", "Enable" },
        { "TerminalHotkey", "Key" },
        { "TerminalHotkey", "Type" },

        { "IdlePowerSaver", "Enable" },
        { "IdlePowerSaver", "Timeout" },
        { "IdlePowerSaver", "ReducedPowerPercent" },
        { "IdlePowerSaver", "MinLoadThreshold" },
        { "IdlePowerSaver", "MaxLoadThreshold" },

        { "BackupSettings", "BackupFileFormat" },

        { "Graph", "Graph1HoursBack" },
        { "Graph", "Graph2HoursBack" },

        { "TimeTracker", "HourlyRate" },
        { "TimeTracker", "WorktimeCounterKey" },
    };
    return settings;
}

const std::vector<SettingName>& ConditionalSettings()
{
    static const std::vector<SettingName> settings{
        /* Only read when [App] RememberWindowSize is on. */
        { "App", "LastWindowSize" },

        /* Optional, so older settings.ini files keep working. */
        { "Macro_Config", "UsePerApplicationMacros" },
        { "Macro_Config", "UseAdvancedKeyBinding" },
        { "Macro_Config", "BringToForegroundKey" },
        { "Macro_Config", "ScriptLauncherKey" },
        { "Sensors", "ListeningIp" },
        { "ModbusMaster", "Device" },

        /* Read for every [Backup_N] section that exists. */
        { "Backup_", "From" },
        { "Backup_", "To" },
        { "Backup_", "Ignore" },
        { "Backup_", "MaxBackups" },
        { "Backup_", "Compress" },
        { "Backup_", "CalculateHash" },
        { "Backup_", "BufferSize" },
    };
    return settings;
}

bool IsDeclared(std::string_view section, std::string_view key)
{
    const auto matches = [&](const SettingName& name)
    {
        if(name.key != key)
            return false;
        return name.section == section || MatchesFamily(section, name.section);
    };

    return std::ranges::any_of(RequiredSettings(), matches) ||
        std::ranges::any_of(ConditionalSettings(), matches);
}

std::vector<std::pair<std::string, std::string>> ReadSettingNames(std::string_view ini_content)
{
    std::vector<std::pair<std::string, std::string>> names;
    std::string section;

    std::size_t offset = 0;
    while(offset <= ini_content.size())
    {
        const std::size_t end = ini_content.find_first_of("\r\n", offset);
        const std::string_view line = Trim(ini_content.substr(offset,
            end == std::string_view::npos ? std::string_view::npos : end - offset));
        if(end == std::string_view::npos)
            offset = ini_content.size() + 1;
        else
            offset = end + 1;

        if(line.empty() || line.front() == '#' || line.front() == ';')
            continue;

        if(line.front() == '[')
        {
            const std::size_t closing = line.find(']');
            if(closing != std::string_view::npos)
                section = std::string(Trim(line.substr(1, closing - 1)));
            continue;
        }

        if(section.empty())
            continue;

        const std::size_t equals = line.find('=');
        if(equals == std::string_view::npos)
            continue;

        const std::string_view key = Trim(line.substr(0, equals));
        if(!key.empty())
            names.emplace_back(section, std::string(key));
    }
    return names;
}

std::vector<SettingName> MissingRequiredSettings(std::string_view ini_content)
{
    const auto defined = ReadSettingNames(ini_content);
    std::vector<SettingName> missing;
    for(const SettingName& required : RequiredSettings())
    {
        const bool present = std::ranges::any_of(defined, [&](const auto& entry)
        {
            return entry.first == required.section && entry.second == required.key;
        });
        if(!present)
            missing.push_back(required);
    }
    return missing;
}

std::uint16_t ParseUsedPages(std::string_view in)
{
    std::uint16_t pages = 0;
    for(const auto& page : PAGE_NAMES)
    {
        if(IContains(in, page.name))
            pages |= page.bit;
    }
    return pages;
}

std::string FormatUsedPages(std::uint16_t pages)
{
    std::string result;
    for(const auto& page : PAGE_NAMES)
    {
        if(pages & page.bit)
        {
            result += page.name;
            result += ", ";
        }
    }
    if(result.size() > 2)
        result.resize(result.size() - 2);
    return result;
}
}
