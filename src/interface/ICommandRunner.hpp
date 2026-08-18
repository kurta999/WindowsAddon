#pragma once

#include <string_view>

class ICommandRunner
{
public:
    virtual ~ICommandRunner() = default;

    // Starts a command without waiting for it to finish. Returns false when
    // the process could not be created.
    [[nodiscard]] virtual bool Start(std::string_view command, bool hidden) = 0;
};
