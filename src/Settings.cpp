#include "pch_core.hpp"

#include "Settings.hpp"
#include "SettingsReader.hpp"
#include "SettingsWriter.hpp"
#include "BackupSettings.hpp"
#include "Logger.hpp"
#include "Utils.hpp"

#include <boost/algorithm/string/join.hpp>
#include <boost/property_tree/ini_parser.hpp>
#include <boost/property_tree/ptree.hpp>

#include <cerrno>
#include <cstdio>
#include <fstream>
#include <ostream>

namespace
{
constexpr const char* SETTINGS_FILE_PATH = "./settings.ini";

std::filesystem::path AbsoluteSettingsPath()
{
    std::error_code error;
    auto path = std::filesystem::absolute(SETTINGS_FILE_PATH, error);
    return error ? std::filesystem::path(SETTINGS_FILE_PATH) : path.lexically_normal();
}

std::string LastStreamError()
{
    if(errno == 0)
        return "stream failure (no operating-system error code was provided)";
    return std::error_code(errno, std::generic_category()).message();
}

}

namespace
{
// Logs every required setting the file does not define, instead of stopping
// at the first one.
void ReportMissingRequiredSettings(const boost::property_tree::ptree& tree,
    const std::filesystem::path& settings_path)
{
    /* Loading stops at the first missing key, so without this the user fixes
       one name, restarts, and finds the next one. Report them all up front. */
    std::vector<std::string> missing;
    for(const auto& setting : settings_schema::RequiredSettings())
    {
        const auto section = tree.get_child_optional(std::string(setting.section));
        if(section && section->find(std::string(setting.key)) != section->not_found())
            continue;
        missing.push_back(std::format("[{}] {}", setting.section, setting.key));
    }

    if(!missing.empty())
    {
        LOG(LogLevel::Error, "Settings file '{}' is missing {} required setting(s): {}",
            settings_path.generic_string(), missing.size(), boost::algorithm::join(missing, ", "));
    }
}
}

void Settings::RegisterBinding(ISettingsBinding& binding)
{
    m_Bindings.push_back(&binding);
}

void Settings::ClearBindings()
{
    m_Bindings.clear();
}

void Settings::SetWindowSizeProvider(WindowSizeProvider provider)
{
    m_WindowSizeProvider = std::move(provider);
}

void Settings::LoadFile()
{
    const auto settings_path = AbsoluteSettingsPath();
    std::error_code file_error;
    const bool settings_file_exists = std::filesystem::exists(settings_path, file_error);
    if(file_error)
    {
        LOG(LogLevel::Critical, "Failed to inspect settings file '{}': {}. No settings were loaded.",
            settings_path.generic_string(), file_error.message());
        used_pages.Enable(settings_schema::Page_Log);
        return;
    }

    if(!settings_file_exists)
    {
        SaveFile(true);
        file_error.clear();
        if(!std::filesystem::exists(settings_path, file_error))
        {
            LOG(LogLevel::Critical,
                "Settings file '{}' was missing and automatic creation failed: {}. No settings were loaded.",
                settings_path.generic_string(), file_error ? file_error.message() : "file was not created");
            used_pages.Enable(settings_schema::Page_Log);
            return;
        }
        LOG(LogLevel::Normal, "Settings file was missing; created defaults at '{}'", settings_path.generic_string());
    }

    boost::property_tree::ptree pt;
    try
    {
        boost::property_tree::ini_parser::read_ini(SETTINGS_FILE_PATH, pt);
    }
    catch(const boost::property_tree::ini_parser::ini_parser_error& e)
    {
        const std::string line = e.line() == 0 ? std::string{} : std::format(" at line {}", e.line());
        LOG(LogLevel::Critical,
            "Failed to parse settings file '{}'{}: {}. No settings were loaded.",
            settings_path.generic_string(), line, e.message());
        used_pages.Enable(settings_schema::Page_Log);
        return;
    }
    catch(const std::exception& e)
    {
        LOG(LogLevel::Critical, "Failed to read settings file '{}': {}. No settings were loaded.",
            settings_path.generic_string(), e.what());
        used_pages.Enable(settings_schema::Page_Log);
        return;
    }

    ReportMissingRequiredSettings(pt, settings_path);

    SettingsReader reader(pt);

    /* Every binding is attempted on its own. One value that cannot be read used
       to stop the whole load, so a typo in an early section silently left every
       later feature at its defaults. */
    for(ISettingsBinding* binding : m_Bindings)
    {
        try
        {
            binding->LoadSettings(reader);
        }
        catch(const std::exception& e)
        {
            LOG(LogLevel::Critical,
                "Failed to load {} from settings file '{}': {}. [{}] keeps its defaults; the rest of the file is still read.",
                reader.Context(), settings_path.generic_string(), e.what(), binding->SettingsSection());
            used_pages.Enable(settings_schema::Page_Log);
        }
        catch(...)
        {
            LOG(LogLevel::Critical,
                "Failed to load {} from settings file '{}': unknown exception. [{}] keeps its defaults.",
                reader.Context(), settings_path.generic_string(), binding->SettingsSection());
            used_pages.Enable(settings_schema::Page_Log);
        }
    }

    if(used_pages.pages == 0)  /* Enable at least the Log panel if everything is disabled */
    {
        used_pages.Enable(settings_schema::Page_Log);
        LOG(LogLevel::Error, "Enabling log panel, because every panel is disabled");
    }
}

void Settings::LoadSettings(SettingsReader& reader)
{
    /* Read first: the panel list decides whether the application has a usable
       window at all, and it used to be loaded in its own group so a failure
       anywhere else in [App] could not cost it. */
    used_pages = ParseUsedPagesFromString(reader.Required("App", "UsedPages"));

    minimize_on_exit = utils::stob(reader.Required("App", "MinimizeOnExit"));
    minimize_on_startup = utils::stob(reader.Required("App", "MinimizeOnStartup"));
    if(m_Logger != nullptr)
        m_Logger->LoadSettingsFrom(reader, "App");
    default_page = utils::stoi<decltype(default_page)>(reader.Required("App", "DefaultPage"));
    remember_window_size = utils::stoi<decltype(remember_window_size)>(reader.Required("App", "RememberWindowSize"));
    if(remember_window_size)
    {
        const auto& last_window_size = reader.Required("App", "LastWindowSize");
        if(sscanf(last_window_size.c_str(), "%d,%d", &window_size.width, &window_size.height) != 2)
        {
            LOG(LogLevel::Error,
                "Invalid setting [App] LastWindowSize: expected 'width,height', got '{}'", last_window_size);
        }

        if(window_size.width < WINDOW_SIZE_X)
            window_size.width = WINDOW_SIZE_X;
        if(window_size.height < WINDOW_SIZE_Y)
            window_size.height = WINDOW_SIZE_Y;
    }
    always_on_numlock = utils::stob(reader.Required("App", "AlwaysOnNumLock"));
    const auto& shared_drive = reader.Required("App", "SharedDriveLetter");
    if(shared_drive.empty())
        throw std::runtime_error("SharedDriveLetter must contain at least one character");
    shared_drive_letter = shared_drive[0];
    crypto_price_update = utils::stoi<uint16_t>(reader.Required("App", "CryptoPriceUpdate"));
}

void Settings::SaveSettings(std::ostream& out) const
{
    SettingsWriter writer(out, "App");
    writer.Key("MinimizeOnExit", minimize_on_exit)
        .Key("MinimizeOnStartup", minimize_on_startup);

    if(m_Logger != nullptr)
        m_Logger->WriteSettingsTo(writer);

    writer.Key("DefaultPage", static_cast<uint16_t>(default_page))
        .Key("UsedPages", settings_schema::FormatUsedPages(used_pages.pages))
        .Key("RememberWindowSize", remember_window_size)
        .Key("LastWindowSize", std::format("{}, {}", window_size.width, window_size.height))
        .Key("AlwaysOnNumLock", always_on_numlock)
        .Key("SharedDriveLetter", shared_drive_letter)
        .Key("CryptoPriceUpdate", crypto_price_update, "Unit: Seconds, 0 = disabled")
        .Blank();
}

void Settings::SaveFile(bool write_default_macros) /* tried boost::ptree ini writer but it doesn't support comments... sticking to plain file functions */
{
    if(write_default_macros)
        used_pages.pages = settings_schema::Page_All;

    /* The remembered size is read back from the window here, not on exit - that
       is deliberate, so "Save" stores what the user currently sees. */
    if(remember_window_size && m_WindowSizeProvider)
    {
        const LogicalSize current = m_WindowSizeProvider();
        if(!current.IsDefault())
            window_size = current;
    }

    errno = 0;
    std::ofstream out(SETTINGS_FILE_PATH, std::ofstream::binary);
    if(!out.is_open())
    {
        LOG(LogLevel::Critical, "Failed to open settings file '{}' for writing: {}",
            AbsoluteSettingsPath().generic_string(), LastStreamError());
        return;
    }

    for(const ISettingsBinding* binding : m_Bindings)
    {
        if(write_default_macros)
            binding->SaveDefaultSettings(out);
        else
            binding->SaveSettings(out);
    }

    errno = 0;
    out.flush();
    if(!out)
    {
        LOG(LogLevel::Critical, "Failed while writing settings file '{}': {}",
            AbsoluteSettingsPath().generic_string(), LastStreamError());
    }
}

void Settings::Init()
{
    SettingsReader::undeclared_setting_reporter = [](std::string_view section, std::string_view key)
    {
        LOG(LogLevel::Warning,
            "Setting [{}] {} is read but not declared in settings_schema", section, key);
    };
    LoadFile();
}
