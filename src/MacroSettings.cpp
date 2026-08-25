#include "pch_core.hpp"
#include "CustomMacro.hpp"
#include "SettingsReader.hpp"
#include "SettingsWriter.hpp"
#include "ScriptLauncher.hpp"
#include "utils/XmlDocument.hpp"
#include "Logger.hpp"
#include "Utils.hpp"

#include <ostream>

/* Reading and writing the macro section of the ini file. */

void MacroSettings::LoadSettings(SettingsReader& reader)
{
    if(auto section = reader.OptionalSection("Macro_Config"))
    {
        utils::ini::ReadValueIfexists(section, "UsePerApplicationMacros", m_Macros.use_per_app_macro);
        utils::ini::ReadValueIfexists(section, "UseAdvancedKeyBinding", m_Macros.advanced_key_binding);
        utils::ini::ReadValueIfexists(section, "BringToForegroundKey", m_Macros.bring_to_foreground_key);
        utils::ini::ReadValueIfexists(section, "ScriptLauncherKey", m_Launcher.launcher_key);
    }

    m_Macros.ResetMacros();

    auto global = std::make_unique<MacroAppProfile>();
    auto& global_child = reader.RequiredSection("Keys_Global");
    for(auto& key : global_child)
    {
        std::string& str = key.second.data();
        reader.Track("Keys_Global", key.first, str);
        m_Macros.ParseMacroKeys(0, key.first, str, global, MacroFlags::None);
    }
    global->app_name = "Global";
    m_Macros.AddMacroProfile(std::move(global));

    /* Per-application macros live in a numbered section family. */
    for(std::size_t counter = 1; reader.CountSection("Keys_Macro" + std::to_string(counter)) == 1; ++counter)
    {
        auto profile = std::make_unique<MacroAppProfile>();
        const std::string section = "Keys_Macro" + std::to_string(counter);
        auto& child = reader.RequiredSection(section);
        for(auto& key : child)
        {
            reader.Track(section, key.first, key.second.data());
            if(key.first == "AppName")
            {
                profile->app_name = key.second.data();
                continue;
            }
            std::string& str = key.second.data();
            m_Macros.ParseMacroKeys(counter, key.first, str, profile, MacroFlags::None);
        }
        m_Macros.AddMacroProfile(std::move(profile));
    }
}

void MacroSettings::SaveMacroConfig(SettingsWriter& writer) const
{
    /* Written from the same table the parser reads, so a new keyword is one
       row rather than a row here and a comment line there. The order follows
       the table, which is ordered to match what settings.example.ini
       already ships. */
    writer.Comment("Possible macro keywords: ");
    for(const macro_keywords::Keyword& keyword : macro_keywords::kAll)
    {
        writer.Comment(std::string(keyword.help));
        if(!keyword.help_alternate.empty())
            writer.Comment(std::string(keyword.help_alternate));
    }
    writer.Blank();

    writer.Section("Macro_Config")
        .Comment("Use per-application macros. AppName is searched in active window title, so window name must contain AppName")
        .Key("UsePerApplicationMacros", m_Macros.use_per_app_macro)
        .Blank()
        .Comment("If enabled, you can bind multiple key combinations with special keys like RSHIFT + 1, but can't bind SHIFT, CTRL and other special keys alone")
        .Key("UseAdvancedKeyBinding", m_Macros.advanced_key_binding)
        .Blank()
        .Comment("If set to valid key, pressing this key will bring this application to foreground or minimize it to the tray")
        .Key("BringToForegroundKey", m_Macros.bring_to_foreground_key)
        .Comment("Key to launch (.py, .js) scripts from file explorer")
        .Key("ScriptLauncherKey", m_Launcher.launcher_key)
        .Blank();
}

void MacroSettings::SaveSettings(std::ostream& out) const
{
    SettingsWriter writer(out);
    SaveMacroConfig(writer);

    /* A macro line is not a "Key = value": the value is assembled from the
       actions themselves, so these go to the stream directly. */
    int cnt = 0;
    for(auto& profile : m_Macros.GetMacros())
    {
        if(!cnt)
            writer.Section("Keys_Global");
        else
            writer.Blank().Section(std::format("Keys_Macro{}", cnt)).Key("AppName", profile->app_name);

        cnt++;
        for(auto& entry : profile->key_vec)
        {
            std::string key = std::format("{} = BIND_NAME[{}]", entry.first, profile->bind_name[entry.first]);
            for(auto& action : entry.second)
                key += action->GenerateText(TextFormat::Ini);
            writer.Out() << key << "\n";
        }
    }
    writer.Blank();
}

void MacroSettings::SaveDefaultSettings(std::ostream& out) const
{
    SettingsWriter writer(out);
    SaveMacroConfig(writer);

    writer.Section("Keys_Global");
    writer.Out() << "NUM_0 = BIND_NAME[global macro 1] KEY_SEQ[A+B+C]\n";
    writer.Out() << "NUM_1 = BIND_NAME[global macro 2] KEY_TYPE[global macro 1]\n";
    writer.Blank().Section("Keys_Macro1").Key("AppName", "Notepad");
    writer.Out() << "NUM_1 = BIND_NAME[close notepad++] KEY_TYPE[test string from WindowsAddon.exe] DELAY[100] KEY_TYPE[Closing window...] DELAY[100-3000] KEY_SEQ[LALT+F4] DELAY[100] KEY_SEQ[RIGHT] KEY_SEQ[ENTER]\n";
    writer.Blank();
}
