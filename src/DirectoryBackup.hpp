#pragma once

#include "utils/CSingleton.hpp"
#include "interface/IBackupEventSink.hpp"
#include "interface/IBackupFileSystem.hpp"
#include <deque>
#include <functional>
#include <future>
#include <filesystem>
#include <mutex>
#include <optional>
#include <string>
#include <vector>
#include "interface/ISettingsBinding.hpp"
#include <iosfwd>
#include <string_view>

class BackupEntry
{
public:
    BackupEntry(std::filesystem::path&& from_, std::vector<std::filesystem::path>&& to_, std::vector<std::string>&& ignore_list_, int max_backups_,
        bool compress, bool calculate_hash_, size_t hash_buf_size_);

    // !\brief Is constructed backup entry valid?
    // !\param fs [in] The filesystem the source path is checked against.
    bool IsValid(const IBackupFileSystem& fs) const;

    // !\brief The largest limit an entry may carry.
    // Rotation walks the destination once per backup, so an absurd limit is a
    // typo rather than an intention.
    static constexpr int max_backups_limit = 10000;

    // !\brief Fold a configured limit into the accepted range.
    // Anything at or below zero means unlimited and is stored as zero.
    [[nodiscard]] static int ClampMaxBackups(long long configured);

    // !\brief Backup source path
    std::filesystem::path from;

    // !\brief Backup destination path vector (for multiple destinations)
    std::vector<std::filesystem::path> to;

    // !\brief Ignored file list. Narrow, because that is what the copy matches
    // against - converting to wide and back lost non-ASCII rules on the way.
    std::vector<std::string> ignore_list;

    // !\brief max backups for backup rotation in destination folder.
    // Zero means unlimited; see BackupEntry::ClampMaxBackups.
    int max_backups;

    // !\brief Calculate hash for backups (hash of destination folder)
    bool calculate_hash;

    // !\brief Compress backed up folder after copy? Currently only 7z is available
    bool m_Compress;

    // !\brief Hash buffer size [MB]
    size_t hash_buf_size;
};

// !\brief The backup jobs: their settings, and running one on demand.
class DirectoryBackup : public ISettingsBinding
{
public:
    // ISettingsBinding - this subsystem owns its own block of settings.ini.
    [[nodiscard]] std::string_view SettingsSection() const override { return "BackupSettings"; }
    void LoadSettings(SettingsReader& reader) override;
    void SaveSettings(std::ostream& out) const override;
    void SaveDefaultSettings(std::ostream& out) const override;

    DirectoryBackup() = default;
    // Cancels and joins a running backup before the state it works on is gone.
    ~DirectoryBackup() override;

    // !\brief Initialize DirectoryBackup
    void Init();

    void SetEventSink(IBackupEventSink* event_sink) noexcept { m_EventSink.store(event_sink, std::memory_order_release); }

    // !\brief The filesystem this works on. Defaults to the real one; a test
    // supplies its own to drive the paths that used to need a real disk.
    void SetFileSystem(IBackupFileSystem& file_system) noexcept { m_FileSystem = &file_system; }
    [[nodiscard]] IBackupFileSystem& FileSystem() const noexcept;

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

    // !\brief How a finished backup is turned into an archive.
    // Returns true only once a usable archive exists; the caller then removes
    // the uncompressed copy. Defaults to CompressBackup, which shells out to
    // 7z - an empty function restores that default.
    using Compressor = std::function<bool(const std::filesystem::path&)>;
    void SetCompressor(Compressor compressor);

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

    // !\brief Drains m_PendingBackups on the worker thread.
    void RunQueuedBackups();

    // !\brief The configured compressor, or the built-in 7z one.
    [[nodiscard]] Compressor GetCompressor();

    IBackupFileSystem* m_FileSystem = nullptr;
    mutable std::mutex m_StateMutex;
    std::vector<BackupEntry> m_Backups;
    std::string m_BackupTimeFormat = "_%Y_%m_%d %H_%M_%S";
    std::string m_CurrentFile;
    Compressor m_Compressor;
    std::atomic<bool> m_IsCancelled{false};

    /* Backups are queued rather than run on top of each other, and the caller
       never waits for one: BackupFile is reached from the GUI thread and from
       the scheduler. m_WorkerMutex guards this queue and backup_future; it is
       never held while waiting on the future. */
    mutable std::mutex m_WorkerMutex;
    std::deque<BackupEntry> m_PendingBackups;
    bool m_WorkerRunning = false;
};
