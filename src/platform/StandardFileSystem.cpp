#include "StandardFileSystem.hpp"

bool StandardFileSystem::Exists(const std::filesystem::path& path) const noexcept
{
    std::error_code error;
    const bool exists = std::filesystem::exists(path, error);
    return exists && !error;
}
