#pragma once

#include "interface/ICommandRunner.hpp"
#include "interface/IFileSystem.hpp"
#include "interface/IScriptCommandResolver.hpp"

#include <filesystem>

enum class ScriptLaunchResult
{
    Started,
    FileNotFound,
    UnsupportedType,
    StartFailed
};

class ScriptLaunchService
{
public:
    ScriptLaunchService(ICommandRunner& command_runner, const IFileSystem& file_system,
                        const IScriptCommandResolver& command_resolver)
        : m_commandRunner(command_runner), m_fileSystem(file_system),
          m_commandResolver(command_resolver)
    {
    }

    [[nodiscard]] ScriptLaunchResult Launch(const std::filesystem::path& script_path);

private:
    ICommandRunner& m_commandRunner;
    const IFileSystem& m_fileSystem;
    const IScriptCommandResolver& m_commandResolver;
};
