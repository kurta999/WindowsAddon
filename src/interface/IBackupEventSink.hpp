#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>

struct BackupSummary
{
    bool success = false;
    std::int64_t duration_ns = 0;
    std::size_t file_count = 0;
    std::size_t bytes_copied = 0;
    std::size_t destination_count = 0;
    std::filesystem::path destination;
};

class IBackupEventSink
{
public:
    virtual ~IBackupEventSink() = default;

    virtual void OnBackupStarted() = 0;
    virtual void OnBackupFinished(const BackupSummary& summary) = 0;
};
