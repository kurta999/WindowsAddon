#pragma once

#include "utils/CSingleton.hpp"
#include "interface/IBackupEventSink.hpp"
#include <future>
#include <filesystem>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

class BackupEntry
{
public:
    BackupEntry(std::filesystem::path&& from_, std::vector<std::filesystem::path>&& to_, std::vector<std::wstring>&& ignore_list_, int max_backups_,
        bool compress, bool calculate_hash_, size_t hash_buf_size_);

    // !\brief Is constructed backup entry valid?
    bool IsValid() const;

    // !\brief Backup source path
    std::filesystem::path from;

    // !\brief Backup destination path vector (for multiple destinations)
    std::vector<std::filesystem::path> to;

    // !\brief Ignored file list
    std::vector<std::wstring> ignore_list;

    // !\brief max backups for backup rotation in destination folder
    int max_backups;

    // !\brief Calculate hash for backups (hash of destination folder)
    bool calculate_hash;

    // !\brief Compress backed up folder after copy? Currently only 7z is available
    bool m_Compress;

    // !\brief Hash buffer size [MB]
    size_t hash_buf_size;
};

class DirectoryBackup : public CSingleton < DirectoryBackup >
{
    friend class CSingleton < DirectoryBackup >;

public:
    DirectoryBackup() = default;
    ~DirectoryBackup() = default;

    // !\brief Initialize DirectoryBackup
    void Init();

    void SetEventSink(IBackupEventSink* event_sink) noexcept { m_EventSink.store(event_sink, std::memory_order_release); }

    // !\brief Construct backup entry from string
    void LoadEntry(const std::string& from, const std::string& to, const std::string& ignore, int max_backups, bool compress_, bool calculate_hash, size_t buffer_size);

    // !\brief Starts backup with given id
    // \param id [in] ID of backup entry to execute
    void BackupFile(int id);

    // !\brief Is backup in progress?
    bool IsInProgress() const;

    // !\brief Delete all backups from backup list
    void Clear();

    std::size_t AddEntry(BackupEntry entry);
    bool RemoveEntry(std::size_t id);
    bool UpdateEntry(std::size_t id, BackupEntry entry);
    [[nodiscard]] std::optional<BackupEntry> GetEntry(std::size_t id) const;
    [[nodiscard]] std::vector<BackupEntry> GetEntries() const;

    void SetBackupTimeFormat(std::string format);
    [[nodiscard]] std::string GetBackupTimeFormat() const;

    void RequestCancel() noexcept { m_IsCancelled.store(true); }
    [[nodiscard]] bool IsCancelled() const noexcept { return m_IsCancelled.load(); }
    [[nodiscard]] std::string GetCurrentFile() const;

protected:
    // !\brief Backups given backup entry
    // !\param backup [in] Backup entry to execute
    void DoBackup(const BackupEntry& backup);

    // !\brief Execute backup rotation (removing older backups)
    // !\param backup [in] Backup entry to execute
    void BackupRotation(const BackupEntry& backup);

    // !\brief Produce a 7z archive and remove the uncompressed directory.
    bool CompressBackup(const std::filesystem::path& dst);

    void RestoreAttributes(const std::filesystem::path& src, const std::filesystem::path& dst);

    // !\brief Future for backup async operations
    std::future<void> backup_future;

    std::atomic<IBackupEventSink*> m_EventSink = nullptr;

private:
    void SetCurrentFile(std::string current_file);

    mutable std::mutex m_StateMutex;
    std::vector<BackupEntry> m_Backups;
    std::string m_BackupTimeFormat = "_%Y_%m_%d %H_%M_%S";
    std::string m_CurrentFile;
    std::atomic<bool> m_IsCancelled{false};
};
