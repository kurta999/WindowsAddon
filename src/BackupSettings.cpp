#include "pch_core.hpp"
#include "BackupSettings.hpp"
#include "BackupSettings.hpp"
#include "DirectoryBackup.hpp"
#include "Logger.hpp"
#include "Utils.hpp"

namespace backup_settings
{
namespace
{
// The separator every list-valued backup setting uses.
constexpr char list_separator = '|';

template <typename Range, typename Project>
[[nodiscard]] auto Join(const Range& items, Project project)
{
    std::decay_t<decltype(project(*std::begin(items)))> joined;
    for(const auto& item : items)
    {
        if(!joined.empty())
            joined += list_separator;
        joined += project(item);
    }
    return joined;
}
}

void AddEntry(DirectoryBackup& backups, const RawEntry& raw)
{
    backups.LoadEntry(raw.from, raw.to, raw.ignore,
        BackupEntry::ClampMaxBackups(utils::stoi<long long>(raw.max_backups)),
        utils::stob(raw.compress),
        utils::stob(raw.calculate_hash),
        utils::stoi<size_t>(raw.buffer_size));
}

RawEntry Format(const BackupEntry& entry)
{
    RawEntry raw;
    raw.from = entry.from.generic_string();
    raw.to = Join(entry.to, [](const std::filesystem::path& path) { return path.generic_string(); });

    raw.ignore = Join(entry.ignore_list, [](const std::string& rule) { return rule; });

    raw.max_backups = std::to_string(entry.max_backups);
    raw.compress = entry.m_Compress ? "1" : "0";
    raw.calculate_hash = entry.calculate_hash ? "1" : "0";
    raw.buffer_size = std::to_string(entry.hash_buf_size);
    return raw;
}
}
