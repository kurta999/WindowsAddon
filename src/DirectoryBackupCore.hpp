#pragma once

#include <atomic>
#include <filesystem>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace backup_core
{
// The strftime format DirectoryBackup appends to a source name by default.
inline constexpr std::string_view default_time_format = "_%Y_%m_%d %H_%M_%S";

// !\brief Can backups named with this format be told apart and rotated?
// False for an empty format, one using a specifier that is not understood, and
// one with no specifier at all - the last would name every backup the same.
[[nodiscard]] bool IsRotatableTimeFormat(std::string_view time_format);

// !\brief Was `candidate_name` produced by this job, under `time_format`?
// Rotation deletes what this accepts, so it matches the format exactly rather
// than settling for a plausible-looking name.
[[nodiscard]] bool IsOwnedBackupName(std::string_view source_name,
                                     std::string_view candidate_name,
                                     std::string_view time_format = default_time_format);
[[nodiscard]] bool ShouldIgnore(const std::filesystem::path& relative_path,
                                const std::vector<std::string>& rules);

// max_backups <= 0 means unlimited. Rotation runs before creating the new
// backup, so enough old entries are returned to leave room for one.
[[nodiscard]] std::vector<std::filesystem::path> RotationCandidates(
    const std::filesystem::path& destination,
    std::string_view source_name,
    int max_backups,
    std::string_view time_format = default_time_format);
[[nodiscard]] bool RotateOwnedBackups(const std::filesystem::path& destination,
                                      std::string_view source_name,
                                      int max_backups,
                                      std::string_view time_format = default_time_format);

struct Options
{
    std::vector<std::string> ignore_rules;
    std::atomic<bool>* cancelled = nullptr;
    bool verify_hash = false;
    // Read size used while hashing; 0 falls back to the default.
    std::size_t hash_buffer_bytes = 0;
    std::function<void(const std::filesystem::path&, const std::filesystem::path&)> after_copy_file;
    std::function<void(const std::filesystem::path&, const std::filesystem::path&)> after_create_directory;
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
    // What reached this destination, not what the source holds - the two differ
    // whenever a backup is cancelled or fails part way.
    std::size_t file_count = 0;
    std::uintmax_t bytes = 0;
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
