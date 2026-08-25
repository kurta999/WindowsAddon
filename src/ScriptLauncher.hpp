#pragma once

#include "automation/ScriptLaunchService.hpp"
#include "interface/IHotkeyHandler.hpp"

class ScriptLauncher : public IHotkeyHandler
{
public:
    // IHotkeyHandler - this feature owns one global key.
    [[nodiscard]] std::string HotkeyBinding() const override { return launcher_key; }
    [[nodiscard]] std::string_view HotkeyOwner() const override { return "ScriptLauncher"; }
    void OnHotkeyPressed() override;

    ScriptLauncher(ICommandRunner& command_runner, const IFileSystem& file_system,
                   const IScriptCommandResolver& command_resolver)
        : m_Service(command_runner, file_system, command_resolver)
    {
    }
    ~ScriptLauncher() = default;

    void Execute();
    [[nodiscard]] ScriptLaunchResult LaunchScript(const std::filesystem::path& script_path);

    // !\brief Key to launch (.py, .js) scripts from file explorer
    std::string launcher_key = "G2";

private:
    ScriptLaunchService m_Service;
};
