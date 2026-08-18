#pragma once

#include <string>
#include <string_view>

class ICommandTextResolver
{
public:
    virtual ~ICommandTextResolver() = default;
    [[nodiscard]] virtual std::string Resolve(std::string_view command_name,
        std::string_view configured_command) const = 0;
};
