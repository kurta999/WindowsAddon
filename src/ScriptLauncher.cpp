#include "pch.hpp"

ScriptLaunchResult ScriptLauncher::LaunchScript(const std::filesystem::path& script_path)
{
    const auto result = m_Service.Launch(script_path);
    switch(result)
    {
        case ScriptLaunchResult::Started:
            break;
        case ScriptLaunchResult::FileNotFound:
            LOG(LogLevel::Error, "Script file does not exist: {}", script_path.generic_string());
            break;
        case ScriptLaunchResult::UnsupportedType:
            LOG(LogLevel::Error, "Unsupported script type: {}", script_path.extension().string());
            break;
        case ScriptLaunchResult::StartFailed:
            LOG(LogLevel::Error, "Script execution could not be started: {}", script_path.generic_string());
            break;
    }
    return result;
}

void ScriptLauncher::Execute()
{
    auto path = utils::GetDestinationPathFromFileExplorer();

    std::vector<std::wstring> items = utils::GetSelectedItemsFromFileExplorer();
    if(items.size() != 1)
        return;

    std::wstring script_path = items[0];

    (void)LaunchScript(std::string(script_path.begin(), script_path.end()));
}
