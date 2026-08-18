#pragma once

#include <filesystem>

class IFileSystem
{
public:
    virtual ~IFileSystem() = default;

    [[nodiscard]] virtual bool Exists(const std::filesystem::path& path) const noexcept = 0;
};
