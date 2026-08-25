#pragma once

#include <cstdint>
#include <filesystem>
#include <unordered_map>

// !\brief Per-directory size and file-count accumulation for a scanned tree.
//
// This bookkeeping lived interleaved with wxTreeList construction in
// FilePanel's scan loop: every file added its size to its directory, then a
// while loop walked ancestors adding the size to each until it reached the
// scan root. That loop compared canonicalised directory paths against the
// root as the user typed it - so a root with a trailing separator never
// compared equal, and the walk ran past it to the drive root, whose parent is
// itself: an infinite loop on the UI thread, reachable by typing a trailing
// backslash into the path box.
//
// Paths handed to this class are compared lexically; the caller canonicalises
// once at the root. The ancestor walk also stops when a parent equals its
// child, so no input can loop it.
class DirectoryUsage
{
public:
    struct Usage
    {
        std::uintmax_t bytes = 0;
        std::size_t direct_files = 0;
    };

    // !\brief `root` as the boundary the ancestor walk stops at. Normalised
    // lexically; the caller passes the canonical scan root.
    explicit DirectoryUsage(const std::filesystem::path& root);

    // !\brief Record one file of `size` bytes in `directory`, crediting the
    // size to the directory and every ancestor up to and including the root.
    void AddFile(const std::filesystem::path& directory, std::uintmax_t size);

    // !\brief The usage of one directory, or null when nothing was recorded.
    [[nodiscard]] const Usage* Find(const std::filesystem::path& directory) const;
    [[nodiscard]] const Usage* FindByKey(std::size_t key) const;

    [[nodiscard]] std::uintmax_t TotalBytes() const { return m_TotalBytes; }
    [[nodiscard]] std::size_t FileCount() const { return m_FileCount; }

    [[nodiscard]] const std::unordered_map<std::size_t, Usage>& All() const { return m_Usage; }

    // !\brief The map key for `path` - the same hashing every lookup uses.
    [[nodiscard]] static std::size_t KeyOf(const std::filesystem::path& path);

private:
    std::filesystem::path m_Root;
    std::unordered_map<std::size_t, Usage> m_Usage;
    std::uintmax_t m_TotalBytes = 0;
    std::size_t m_FileCount = 0;
};
