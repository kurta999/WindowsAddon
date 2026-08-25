#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <variant>

enum class SimpleNotificationKind
{
    ScreenshotSaveFailed,
    SettingsSaved,
    StringEscaped,
    TxListLoaded,
    TxListSaved,
    RxListLoaded,
    RxListSaved,
    FrameMappingLoaded,
    FrameMappingSaved,
    TxListLoadError,
    RxListLoadError,
    FrameMappingLoadError,
    /* The panel used to report "Saved" whatever the saver returned, so a save
       that failed - a read-only file, a full disk - looked like it worked. */
    TxListSaveError,
    RxListSaveError,
    FrameMappingSaveError,
    DidUpdated,
    EverythingSaved,
    SelectedLogsCopied
};

struct SimpleNotification
{
    SimpleNotificationKind kind;
};

enum class SavedFileKind
{
    Screenshot,
    CanLog,
    ModbusLog,
    Commands,
    DidCache
};

struct FileSavedNotification
{
    SavedFileKind kind;
    std::int64_t duration_ns;
    std::string filename;
};

struct PathSeparatorsReplacedNotification
{
    std::string path;
};

struct BackupCompletedNotification
{
    std::int64_t duration_ns;
    std::size_t file_count;
    std::size_t bytes_copied;
    std::size_t destination_count;
    std::filesystem::path destination;
};

struct BackupFailedNotification
{
    std::filesystem::path destination;
};

struct AlarmSetupNotification
{
    std::string name;
    std::chrono::seconds duration;
};

struct AlarmTriggeredNotification
{
    std::string name;
};

struct WorktimeToggledNotification
{
    bool working;
    std::chrono::seconds duration;
};

// Type-safe event queue: every alternative carries exactly the data required
// by its notification. Producers can no longer create invalid any-based tuples.
using AppNotification = std::variant<
    SimpleNotification,
    FileSavedNotification,
    PathSeparatorsReplacedNotification,
    BackupCompletedNotification,
    BackupFailedNotification,
    AlarmSetupNotification,
    AlarmTriggeredNotification,
    WorktimeToggledNotification>;
