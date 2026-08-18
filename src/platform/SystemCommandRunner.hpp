#pragma once

#include "interface/ICommandRunner.hpp"

class SystemCommandRunner final : public ICommandRunner
{
public:
    [[nodiscard]] bool Start(std::string_view command, bool hidden) override;
};
