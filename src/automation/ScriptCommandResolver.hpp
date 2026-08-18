#pragma once

#include "interface/IScriptCommandResolver.hpp"

#include <string>
#include <vector>

struct ScriptInterpreter
{
    std::string extension;
    std::string executable;
};

// Registry-backed Strategy for mapping a script type to its interpreter.
class ScriptCommandResolver final : public IScriptCommandResolver
{
public:
    ScriptCommandResolver();
    explicit ScriptCommandResolver(std::vector<ScriptInterpreter> interpreters);

    [[nodiscard]] std::optional<std::string> Resolve(
        const std::filesystem::path& script_path) const override;

private:
    std::vector<ScriptInterpreter> m_Interpreters;
};
