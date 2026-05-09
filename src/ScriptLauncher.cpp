#include "pch.hpp"

void ScriptLauncher::LaunchScript(const std::filesystem::path& script_path)
{
    std::filesystem::path path(script_path);
    if (!std::filesystem::exists(path))
    {
        LOG(LogLevel::Error, "Script file does not exist: {}", script_path.generic_string());
        return;
    }

    std::string ext = path.extension().string();
    std::string command;

#ifdef _WIN32
    const char* node_cmd = "node";
    const char* python_cmd = "python";
#else
    const char* node_cmd = "node";
    const char* python_cmd = "python3";
#endif

    if (ext == ".js")
    {
        command = std::string(node_cmd) + " \"" + path.string() + "\"";
    }
    else if (ext == ".py")
    {
        command = std::string(python_cmd) + " \"" + path.string() + "\"";
    }
    else
    {
        LOG(LogLevel::Error, "Unsupported script type: {}", ext);
        return;
    }

    int ret = std::system(command.c_str());
    if (ret != 0)
    {
        LOG(LogLevel::Error, "Script execution failed with code: {}", ext);
    }
}

void ScriptLauncher::Execute()
{
    auto path = utils::GetDestinationPathFromFileExplorer();

    std::vector<std::wstring> items = utils::GetSelectedItemsFromFileExplorer();
    if(items.size() != 1)
        return;

    std::wstring script_path = items[0];

    LaunchScript(std::string(script_path.begin(), script_path.end()));
}