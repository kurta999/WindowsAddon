#include "DirectoryBackupCore.hpp"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <span>

namespace backup_core
{
namespace
{
bool IsDigitAt(std::string_view text, std::size_t index)
{
    return index < text.size() && std::isdigit(static_cast<unsigned char>(text[index])) != 0;
}

constexpr std::size_t default_hash_buffer_bytes = 64 * 1024;

std::uint64_t FileHash(const std::filesystem::path& path, std::error_code& error, std::span<char> buffer)
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
    while(input)
    {
        input.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
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

// How many digits strftime writes for the specifiers a backup name can sensibly
// use. Zero means "not understood", which makes the whole format unrotatable
// rather than letting rotation guess at names it might delete.
int DigitsForSpecifier(char specifier)
{
    switch(specifier)
    {
        case 'Y': return 4;
        case 'j': return 3;
        case 'C': case 'd': case 'e': case 'H': case 'I': case 'm':
        case 'M': case 'S': case 'U': case 'W': case 'y': return 2;
        default: return 0;
    }
}

// Does `text` look like `format` with every specifier filled in?
bool MatchesTimeFormat(std::string_view text, std::string_view format)
{
    std::size_t index = 0;
    for(std::size_t f = 0; f < format.size(); ++f)
    {
        if(format[f] != '%')
        {
            if(index >= text.size() || text[index] != format[f])
                return false;
            ++index;
            continue;
        }

        if(++f >= format.size())
            return false;
        if(format[f] == '%')
        {
            if(index >= text.size() || text[index] != '%')
                return false;
            ++index;
            continue;
        }

        const int digits = DigitsForSpecifier(format[f]);
        if(digits == 0)
            return false;
        for(int digit = 0; digit < digits; ++digit)
        {
            if(!IsDigitAt(text, index))
                return false;
            ++index;
        }
    }
    return index == text.size();
}

// A backup is copied under this suffix and renamed into place only once it
// is complete, so a cancelled run - or a killed process - can never leave
// something that looks like a finished backup.
constexpr std::string_view incomplete_suffix = ".incomplete";

bool IsCancelled(const Options& options)
{
    return options.cancelled && options.cancelled->load();
}

// A destination under the source would be walked into while it is being
// written, copying the backup into itself until the path length or the disk
// runs out. Cheaper to refuse than to detect halfway through.
bool IsInside(const std::filesystem::path& parent, const std::filesystem::path& child)
{
    std::error_code error;
    const auto parent_resolved = std::filesystem::weakly_canonical(parent, error);
    if(error)
        return false;
    const auto child_resolved = std::filesystem::weakly_canonical(child, error);
    if(error)
        return false;

    const auto relative = child_resolved.lexically_relative(parent_resolved);
    return !relative.empty() && *relative.begin() != "..";
}
}

bool IsRotatableTimeFormat(std::string_view time_format)
{
    bool has_specifier = false;
    for(std::size_t index = 0; index < time_format.size(); ++index)
    {
        if(time_format[index] != '%')
            continue;
        if(++index >= time_format.size())
            return false;  /* a trailing '%' is not a format */
        if(time_format[index] == '%')
            continue;      /* an escaped percent is a literal, not a specifier */
        if(DigitsForSpecifier(time_format[index]) == 0)
            return false;
        has_specifier = true;
    }
    // Without one, every backup would be named the same and none could be told
    // apart from the one before it.
    return has_specifier;
}

bool IsOwnedBackupName(std::string_view source_name, std::string_view candidate_name,
                       std::string_view time_format)
{
    // Rotation deletes whatever this accepts, so a name only counts when the
    // whole timestamp matches the format that produced it.
    if(!IsRotatableTimeFormat(time_format))
        return false;
    if(candidate_name.ends_with(".7z"))
        candidate_name.remove_suffix(3);
    if(!candidate_name.starts_with(source_name))
        return false;

    return MatchesTimeFormat(candidate_name.substr(source_name.size()), time_format);
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
                                                       int max_backups,
                                                       std::string_view time_format)
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
        if(IsOwnedBackupName(source_name, item.path().filename().string(), time_format))
            owned.push_back(item.path());
    }
    std::ranges::sort(owned, {}, [](const auto& path) { return path.filename().string(); });

    const std::size_t keep_before_new = static_cast<std::size_t>(max_backups - 1);
    if(owned.size() <= keep_before_new)
        return {};
    owned.resize(owned.size() - keep_before_new);
    return owned;
}

bool RotateOwnedBackups(const std::filesystem::path& destination, std::string_view source_name,
                        int max_backups, std::string_view time_format)
{
    bool success = true;

    /* Partials from a run that never finished are scrap regardless of the
       limit, and they are deliberately not counted as backups. */
    std::error_code sweep_error;
    std::filesystem::directory_iterator sweep(destination,
        std::filesystem::directory_options::skip_permission_denied, sweep_error);
    if(!sweep_error)
    {
        for(const auto& item : sweep)
        {
            const std::string filename = item.path().filename().string();
            std::string_view name = filename;
            if(!name.ends_with(incomplete_suffix))
                continue;
            name.remove_suffix(incomplete_suffix.size());
            if(!IsOwnedBackupName(source_name, name, time_format))
                continue;

            std::error_code error;
            std::filesystem::remove_all(item.path(), error);
            success = success && !error;
        }
    }
    for(const auto& path : RotationCandidates(destination, source_name, max_backups, time_format))
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

    // One buffer for the whole run; hashing never retains a file in memory.
    std::vector<char> hash_buffer(options.verify_hash
        ? std::max(options.hash_buffer_bytes, default_hash_buffer_bytes) : 0);

    for(const auto& root : destination_roots)
    {
        DestinationResult result;
        result.destination = root / backup_name;
        std::filesystem::path staging = result.destination;
        staging += incomplete_suffix;
        if(IsCancelled(options))
        {
            result.cancelled = true;
            result.error = "cancelled";
            results.push_back(std::move(result));
            continue;
        }

        if(IsInside(source, result.destination))
        {
            result.error = "destination is inside the source";
            results.push_back(std::move(result));
            continue;
        }

        /* Two backups of the same source cannot share a name. This is what a
           time format with no time in it produces, and the plain message beats
           the rename failure it would otherwise become. */
        if(std::filesystem::exists(result.destination))
        {
            result.error = "a backup of this name already exists";
            results.push_back(std::move(result));
            continue;
        }

        std::error_code error;
        // Whatever an earlier run left under this exact name is scrap.
        std::filesystem::remove_all(staging, error);
        error.clear();
        std::filesystem::create_directories(staging, error);
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

            const auto target = staging / relative;
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
                if(!error && options.after_create_directory)
                    options.after_create_directory(item.path(), target);
            }
            else if(item.is_regular_file(error))
            {
                std::filesystem::create_directories(target.parent_path(), error);
                if(!error) std::filesystem::copy_file(item.path(), target, error);
                if(!error)
                {
                    /* Sized from the copy rather than the source, and through
                       its own error code so a size that cannot be read does not
                       fail the backup. */
                    std::error_code size_error;
                    const auto size = std::filesystem::file_size(target, size_error);
                    ++result.file_count;
                    if(!size_error)
                        result.bytes += size;
                }
                if(!error && options.after_copy_file)
                    options.after_copy_file(item.path(), target);
                if(!error && options.verify_hash)
                {
                    std::error_code source_error, target_error;
                    const auto source_hash = FileHash(item.path(), source_error, hash_buffer);
                    const auto target_hash = FileHash(target, target_error, hash_buffer);
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
            std::filesystem::rename(staging, result.destination, error);
            if(error)
                result.error = error.message();
        }
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

        /* An incomplete copy is never left behind: it carries a valid backup
           name, so rotation would count it and delete a real backup to make
           room for it. */
        std::error_code cleanup;
        std::filesystem::remove_all(staging, cleanup);
        results.push_back(std::move(result));
    }
    return results;
}
}
