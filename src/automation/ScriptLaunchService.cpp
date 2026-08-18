#include "ScriptLaunchService.hpp"

ScriptLaunchResult ScriptLaunchService::Launch(const std::filesystem::path& script_path)
{
    if(!m_fileSystem.Exists(script_path))
        return ScriptLaunchResult::FileNotFound;

    const auto command = m_commandResolver.Resolve(script_path);
    if(!command)
        return ScriptLaunchResult::UnsupportedType;

    return m_commandRunner.Start(*command, false)
        ? ScriptLaunchResult::Started
        : ScriptLaunchResult::StartFailed;
}
