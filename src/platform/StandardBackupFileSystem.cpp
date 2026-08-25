#include "StandardBackupFileSystem.hpp"

bool StandardBackupFileSystem::Exists(const std::filesystem::path& path) const noexcept
{
    std::error_code error;
    return std::filesystem::exists(path, error) && !error;
}

bool StandardBackupFileSystem::IsRegularFile(const std::filesystem::path& path) const noexcept
{
    std::error_code error;
    return std::filesystem::is_regular_file(path, error) && !error;
}

bool StandardBackupFileSystem::IsDirectory(const std::filesystem::path& path) const noexcept
{
    std::error_code error;
    return std::filesystem::is_directory(path, error) && !error;
}

bool StandardBackupFileSystem::CreateDirectories(const std::filesystem::path& path, std::error_code& error)
{
    error.clear();
    if(std::filesystem::exists(path, error))
        return !error;

    error.clear();
    std::filesystem::create_directories(path, error);
    return !error;
}
