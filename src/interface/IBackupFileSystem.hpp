#pragma once

#include <filesystem>
#include <system_error>

// !\brief The filesystem operations the backup subsystem performs directly.
//
// IFileSystem stays at its single Exists method because that is all
// ScriptLaunchService needs, and widening it would make that dependency fatter
// for no reason. Backup needs four operations, so it gets its own role - which
// is also why DirectoryBackup never adopted IFileSystem and went on calling
// std::filesystem:: directly, leaving its injection point real only on paper.
class IBackupFileSystem
{
public:
    virtual ~IBackupFileSystem() = default;

    [[nodiscard]] virtual bool Exists(const std::filesystem::path& path) const noexcept = 0;
    [[nodiscard]] virtual bool IsRegularFile(const std::filesystem::path& path) const noexcept = 0;
    [[nodiscard]] virtual bool IsDirectory(const std::filesystem::path& path) const noexcept = 0;

    // !\brief Create the directory and any missing parents.
    // !\return True when the directory exists afterwards, whether or not this
    // call was the one that created it.
    virtual bool CreateDirectories(const std::filesystem::path& path, std::error_code& error) = 0;
};
