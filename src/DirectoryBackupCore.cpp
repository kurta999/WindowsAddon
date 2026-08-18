#include "DirectoryBackupCore.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <fstream>

namespace backup_core
{
namespace
{
bool IsDigitAt(std::string_view text, std::size_t index)
{
    return index < text.size() && std::isdigit(static_cast<unsigned char>(text[index])) != 0;
}

std::uint64_t FileHash(const std::filesystem::path& path, std::error_code& error)
{
    std::ifstream input(path, std::ios::binary);
    if(!input)
    {
        error = std::make_error_code(std::errc::permission_denied);
        return 0;
    }

    // FNV-1a is used here as a fast integrity check, not as an authenticity
    // primitive. It detects copy corruption without retaining files in memory.
    std::uint64_t hash = 14695981039346656037ull;
    std::array<char, 64 * 1024> buffer{};
    while(input)
    {
        input.read(buffer.data(), buffer.size());
        const auto count = input.gcount();
        for(std::streamsize i = 0; i < count; ++i)
        {
            hash ^= static_cast<unsigned char>(buffer[static_cast<std::size_t>(i)]);
            hash *= 1099511628211ull;
        }
    }
    if(!input.eof())
        error = std::make_error_code(std::errc::io_error);
    return hash;
}

bool IsCancelled(const Options& options)
{
    return options.cancelled && options.cancelled->load();
}
}

bool IsOwnedBackupName(std::string_view source_name, std::string_view candidate_name)
{
    // Exact shape produced by DirectoryBackup::backup_time_format:
    // <source>_YYYY_MM_DD HH_MM_SS[.7z]
    if(candidate_name.ends_with(".7z"))
        candidate_name.remove_suffix(3);
    if(!candidate_name.starts_with(source_name))
        return false;

    const auto suffix = candidate_name.substr(source_name.size());
    if(suffix.size() != 20 || suffix[0] != '_' || suffix[5] != '_' || suffix[8] != '_' ||
       suffix[11] != ' ' || suffix[14] != '_' || suffix[17] != '_')
        return false;
    for(const std::size_t index : {1u, 2u, 3u, 4u, 6u, 7u, 9u, 10u, 12u, 13u, 15u, 16u, 18u, 19u})
        if(!IsDigitAt(suffix, index)) return false;
    return true;
}

bool ShouldIgnore(const std::filesystem::path& relative_path, const std::vector<std::string>& rules)
{
    const auto text = relative_path.generic_string();
    return std::ranges::any_of(rules, [&text](const std::string& rule) {
        return !rule.empty() && text.find(rule) != std::string::npos;
    });
}

std::vector<std::filesystem::path> RotationCandidates(const std::filesystem::path& destination,
                                                       std::string_view source_name,
                                                       int max_backups)
{
    std::vector<std::filesystem::path> owned;
    if(max_backups <= 0)
        return owned;

    std::error_code error;
    std::filesystem::directory_iterator iterator(destination,
        std::filesystem::directory_options::skip_permission_denied, error);
    if(error)
        return owned;
    for(const auto& item : iterator)
    {
        if(IsOwnedBackupName(source_name, item.path().filename().string()))
            owned.push_back(item.path());
    }
    std::ranges::sort(owned, {}, [](const auto& path) { return path.filename().string(); });

    const std::size_t keep_before_new = static_cast<std::size_t>(max_backups - 1);
    if(owned.size() <= keep_before_new)
        return {};
    owned.resize(owned.size() - keep_before_new);
    return owned;
}

bool RotateOwnedBackups(const std::filesystem::path& destination, std::string_view source_name, int max_backups)
{
    bool success = true;
    for(const auto& path : RotationCandidates(destination, source_name, max_backups))
    {
        // RotationCandidates only returns direct children with an exact owned
        // name, keeping recursive deletion scoped to the destination.
        std::error_code error;
        std::filesystem::remove_all(path, error);
        success = success && !error;
    }
    return success;
}

SourceMetrics MeasureSource(const std::filesystem::path& source, const std::vector<std::string>& ignore_rules)
{
    SourceMetrics metrics;
    std::error_code error;
    std::filesystem::recursive_directory_iterator iterator(source,
        std::filesystem::directory_options::skip_permission_denied, error);
    const std::filesystem::recursive_directory_iterator end;
    while(!error && iterator != end)
    {
        const auto& item = *iterator;
        const auto relative = item.path().lexically_relative(source);
        if(ShouldIgnore(relative, ignore_rules))
        {
            if(item.is_directory(error))
                iterator.disable_recursion_pending();
            error.clear();
            iterator.increment(error);
            continue;
        }

        if(item.is_regular_file(error))
        {
            const auto size = item.file_size(error);
            if(!error)
            {
                ++metrics.file_count;
                metrics.bytes += size;
            }
        }
        if(!error)
            iterator.increment(error);
    }

    metrics.success = !error;
    if(error)
        metrics.error = error.message();
    return metrics;
}

std::vector<DestinationResult> CopyToDestinations(const std::filesystem::path& source,
                                                   const std::vector<std::filesystem::path>& destination_roots,
                                                   const std::filesystem::path& backup_name,
                                                   const Options& options)
{
    std::vector<DestinationResult> results;
    results.reserve(destination_roots.size());

    for(const auto& root : destination_roots)
    {
        DestinationResult result;
        result.destination = root / backup_name;
        if(IsCancelled(options))
        {
            result.cancelled = true;
            result.error = "cancelled";
            results.push_back(std::move(result));
            continue;
        }

        std::error_code error;
        std::filesystem::create_directories(result.destination, error);
        if(error)
        {
            result.error = error.message();
            results.push_back(std::move(result));
            continue;
        }

        std::filesystem::recursive_directory_iterator iterator(source,
            std::filesystem::directory_options::skip_permission_denied, error);
        const std::filesystem::recursive_directory_iterator end;
        while(!error && iterator != end)
        {
            if(IsCancelled(options))
            {
                result.cancelled = true;
                result.error = "cancelled";
                break;
            }

            const auto& item = *iterator;
            const auto relative = item.path().lexically_relative(source);
            if(ShouldIgnore(relative, options.ignore_rules))
            {
                if(item.is_directory(error)) iterator.disable_recursion_pending();
                error.clear();
                iterator.increment(error);
                continue;
            }

            const auto target = result.destination / relative;
            if(item.is_symlink(error))
            {
                const auto link_target = std::filesystem::read_symlink(item.path(), error);
                if(!error)
                {
                    std::filesystem::create_directories(target.parent_path(), error);
                    if(!error) std::filesystem::create_symlink(link_target, target, error);
                }
            }
            else if(item.is_directory(error))
            {
                std::filesystem::create_directories(target, error);
            }
            else if(item.is_regular_file(error))
            {
                std::filesystem::create_directories(target.parent_path(), error);
                if(!error) std::filesystem::copy_file(item.path(), target, error);
                if(!error && options.after_copy_file)
                    options.after_copy_file(item.path(), target);
                if(!error && options.verify_hash)
                {
                    std::error_code source_error, target_error;
                    const auto source_hash = FileHash(item.path(), source_error);
                    const auto target_hash = FileHash(target, target_error);
                    if(source_error || target_error || source_hash != target_hash)
                    {
                        result.hash_mismatch = true;
                        error = std::make_error_code(std::errc::io_error);
                    }
                }
            }

            if(!error)
                iterator.increment(error);
        }

        if(error && result.error.empty())
            result.error = result.hash_mismatch ? "hash mismatch" : error.message();
        if(!error && !result.cancelled)
        {
            result.success = true;
            if(options.compressor)
            {
                if(options.compressor(result.destination))
                {
                    std::filesystem::remove_all(result.destination, error);
                    result.compressed = !error;
                    result.success = !error;
                    if(error) result.error = error.message();
                }
                else
                {
                    result.success = false;
                    result.error = "compression failed";
                    // Deliberately retain the uncompressed directory.
                }
            }
        }
        results.push_back(std::move(result));
    }
    return results;
}
}
