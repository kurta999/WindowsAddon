#include "DirectoryUsage.hpp"

namespace
{
// !\brief One spelling per directory: dot segments folded, and the trailing
// separator - which lexically_normal keeps, as an empty final element -
// stripped, so "C:/foo/" and "C:/foo" are the same key and the same loop
// boundary. The trailing separator is exactly how the old code's root
// comparison failed.
std::filesystem::path Normalise(const std::filesystem::path& path)
{
    std::filesystem::path normal = path.lexically_normal();
    if(!normal.has_filename() && normal.has_relative_path())
        normal = normal.parent_path();
    return normal;
}
}

DirectoryUsage::DirectoryUsage(const std::filesystem::path& root) :
    m_Root(Normalise(root))
{
}

std::size_t DirectoryUsage::KeyOf(const std::filesystem::path& path)
{
    return std::filesystem::hash_value(Normalise(path));
}

void DirectoryUsage::AddFile(const std::filesystem::path& directory, std::uintmax_t size)
{
    const std::filesystem::path normal = Normalise(directory);

    Usage& direct = m_Usage[std::filesystem::hash_value(normal)];
    direct.bytes += size;
    direct.direct_files++;

    m_TotalBytes += size;
    m_FileCount++;

    /* Credit every ancestor up to and including the root. The equality guard
       is what makes a directory that can never reach the root - the old
       code's trailing-slash case - terminate at the filesystem root instead
       of spinning on it forever. */
    std::filesystem::path current = normal;
    while(current != m_Root)
    {
        const std::filesystem::path parent = current.parent_path();
        if(parent == current)
            break;
        m_Usage[std::filesystem::hash_value(parent)].bytes += size;
        current = parent;
    }
}

const DirectoryUsage::Usage* DirectoryUsage::Find(const std::filesystem::path& directory) const
{
    return FindByKey(KeyOf(directory));
}

const DirectoryUsage::Usage* DirectoryUsage::FindByKey(std::size_t key) const
{
    const auto it = m_Usage.find(key);
    return it != m_Usage.end() ? &it->second : nullptr;
}
