#pragma once

#include <string>

class BackupEntry;
class DirectoryBackup;

// The mapping between one [Backup_N] section of settings.ini and one
// BackupEntry. It lives apart from Settings.cpp so a test can drive it without
// loading a whole settings file, which is what let `BufferSize` be read through
// a truth test into a bool for as long as it was.
namespace backup_settings
{
// !\brief One [Backup_N] section exactly as it sits in the file, before typing.
struct RawEntry
{
    std::string from;
    std::string to;
    std::string ignore;
    std::string max_backups;
    std::string compress;
    std::string calculate_hash;
    std::string buffer_size;
};

// !\brief Types `raw` and appends it to `backups`.
// Throws, the way every other required settings read does, when a number is
// not a number - loading stops and the reason is reported rather than a
// silently wrong value being kept.
void AddEntry(DirectoryBackup& backups, const RawEntry& raw);

// !\brief What the save path writes for `entry`. The inverse of AddEntry.
[[nodiscard]] RawEntry Format(const BackupEntry& entry);
}
