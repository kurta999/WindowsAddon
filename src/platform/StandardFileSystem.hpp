#pragma once

#include "interface/IFileSystem.hpp"

class StandardFileSystem final : public IFileSystem
{
public:
    [[nodiscard]] bool Exists(const std::filesystem::path& path) const noexcept override;
};
