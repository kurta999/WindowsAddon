#include "CommandTextResolver.hpp"

#include <chrono>
#include <format>
#include <utility>

CommandTextResolver::CommandTextResolver()
{
    Register("Set date", [](std::string_view configured_command)
    {
#ifdef _WIN32
        (void)configured_command;
        const auto now = std::chrono::current_zone()->to_local(std::chrono::system_clock::now());
        const auto seconds = std::chrono::floor<std::chrono::seconds>(now);
        return std::format("adb shell \"date -s {:%Y-%m-%d} && date -s {:%H:%M:%OS}\"", seconds, seconds);
#else
        return std::string(configured_command);
#endif
    });
}

void CommandTextResolver::Register(std::string command_name, Rule rule)
{
    m_rules.insert_or_assign(std::move(command_name), std::move(rule));
}

std::string CommandTextResolver::Resolve(std::string_view command_name, std::string_view configured_command) const
{
    const auto rule = m_rules.find(std::string(command_name));
    return rule == m_rules.end() ? std::string(configured_command) : rule->second(configured_command);
}
