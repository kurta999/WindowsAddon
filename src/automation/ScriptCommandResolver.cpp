#include "ScriptCommandResolver.hpp"

#include <algorithm>
#include <cctype>
#include <utility>

namespace
{
std::string Lowercase(std::string value)
{
    std::ranges::transform(value, value.begin(), [](unsigned char character) {
        return static_cast<char>(std::tolower(character));
    });
    return value;
}

std::vector<ScriptInterpreter> PlatformInterpreters()
{
    return {
        {".js", "node"},
#ifdef _WIN32
        {".py", "python"}
#else
        {".py", "python3"}
#endif
    };
}
}

ScriptCommandResolver::ScriptCommandResolver() :
    ScriptCommandResolver(PlatformInterpreters())
{}

ScriptCommandResolver::ScriptCommandResolver(std::vector<ScriptInterpreter> interpreters) :
    m_Interpreters(std::move(interpreters))
{
    for(auto& interpreter : m_Interpreters)
        interpreter.extension = Lowercase(std::move(interpreter.extension));
}

std::optional<std::string> ScriptCommandResolver::Resolve(
    const std::filesystem::path& script_path) const
{
    const auto extension = Lowercase(script_path.extension().string());
    const auto interpreter = std::ranges::find(
        m_Interpreters, extension, &ScriptInterpreter::extension);
    if(interpreter == m_Interpreters.end())
        return std::nullopt;

    return interpreter->executable + " \"" + script_path.string() + "\"";
}
