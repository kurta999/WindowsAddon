#include "utils/CSingleton.hpp"

#include "Settings.hpp"

class ScriptLauncher : public CSingleton < ScriptLauncher >
{
    friend class CSingleton < ScriptLauncher >;

public:
    ScriptLauncher() = default;
    ~ScriptLauncher() = default;

    void Execute();
    void LaunchScript(const std::filesystem::path& script_path);

    // !\brief Key to launch (.py, .js) scripts from file explorer
    std::string launcher_key = "G2";
};