#pragma once

#include "interface/ICommandTextResolver.hpp"

#include <functional>
#include <string>
#include <unordered_map>

// Strategy registry: built-in command preparation is extensible without
// adding name-based branches to Command itself.
class CommandTextResolver final : public ICommandTextResolver
{
public:
    using Rule = std::function<std::string(std::string_view configured_command)>;

    CommandTextResolver();
    void Register(std::string command_name, Rule rule);

    [[nodiscard]] std::string Resolve(std::string_view command_name,
        std::string_view configured_command) const override;

private:
    std::unordered_map<std::string, Rule> m_rules;
};
