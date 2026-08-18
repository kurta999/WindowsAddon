#pragma once

#include <filesystem>
#include <optional>
#include <string>

class IScriptCommandResolver
{
public:
    virtual ~IScriptCommandResolver() = default;

    // Returns an executable command for a supported script, or no value when
    // this strategy does not support the file type.
    [[nodiscard]] virtual std::optional<std::string> Resolve(
        const std::filesystem::path& script_path) const = 0;
};
