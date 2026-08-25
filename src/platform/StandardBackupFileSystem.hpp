#pragma once

#include "interface/IBackupFileSystem.hpp"

// !\brief IBackupFileSystem on top of std::filesystem.
class StandardBackupFileSystem final : public IBackupFileSystem
{
public:
    [[nodiscard]] bool Exists(const std::filesystem::path& path) const noexcept override;
    [[nodiscard]] bool IsRegularFile(const std::filesystem::path& path) const noexcept override;
    [[nodiscard]] bool IsDirectory(const std::filesystem::path& path) const noexcept override;
    bool CreateDirectories(const std::filesystem::path& path, std::error_code& error) override;
};
