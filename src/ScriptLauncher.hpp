#pragma once

#include "automation/ScriptLaunchService.hpp"

class ScriptLauncher
{
public:
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
