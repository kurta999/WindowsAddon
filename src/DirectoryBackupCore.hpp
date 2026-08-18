#pragma once

#include <atomic>
#include <filesystem>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace backup_core
{
[[nodiscard]] bool IsOwnedBackupName(std::string_view source_name, std::string_view candidate_name);
[[nodiscard]] bool ShouldIgnore(const std::filesystem::path& relative_path,
                                const std::vector<std::string>& rules);

// max_backups <= 0 means unlimited. Rotation runs before creating the new
// backup, so enough old entries are returned to leave room for one.
[[nodiscard]] std::vector<std::filesystem::path> RotationCandidates(
    const std::filesystem::path& destination,
    std::string_view source_name,
    int max_backups);
[[nodiscard]] bool RotateOwnedBackups(const std::filesystem::path& destination,
                                      std::string_view source_name,
                                      int max_backups);

struct Options
{
    std::vector<std::string> ignore_rules;
    std::atomic<bool>* cancelled = nullptr;
    bool verify_hash = false;
    std::function<void(const std::filesystem::path&, const std::filesystem::path&)> after_copy_file;
    // Return true only after a usable archive has been produced.
    std::function<bool(const std::filesystem::path&)> compressor;
};

struct DestinationResult
{
    std::filesystem::path destination;
    bool success = false;
    bool cancelled = false;
    bool hash_mismatch = false;
    bool compressed = false;
    std::string error;
};

struct SourceMetrics
{
    std::size_t file_count = 0;
    std::uintmax_t bytes = 0;
    bool success = false;
    std::string error;
};

[[nodiscard]] SourceMetrics MeasureSource(const std::filesystem::path& source,
                                          const std::vector<std::string>& ignore_rules = {});

[[nodiscard]] std::vector<DestinationResult> CopyToDestinations(
    const std::filesystem::path& source,
    const std::vector<std::filesystem::path>& destination_roots,
    const std::filesystem::path& backup_name,
    const Options& options = {});
}
