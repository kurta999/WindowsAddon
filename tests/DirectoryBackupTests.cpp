#include "TestFramework.hpp"

#include "DirectoryBackupCore.hpp"

#include <atomic>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace
{
class TempDirectory
{
public:
    TempDirectory()
    {
        static std::atomic<unsigned> sequence{};
        path = std::filesystem::temp_directory_path() /
            ("WindowsHelperTests_" + std::to_string(sequence.fetch_add(1)));
        std::filesystem::remove_all(path);
        std::filesystem::create_directories(path);
    }
    ~TempDirectory()
    {
        std::error_code error;
        std::filesystem::remove_all(path, error);
    }
    std::filesystem::path path;
};

void WriteFile(const std::filesystem::path& path, std::string_view contents)
{
    std::filesystem::create_directories(path.parent_path());
    std::ofstream output(path, std::ios::binary);
    output << contents;
}
}

TEST_CASE(BackupRotationDeletesOnlyEntriesOwnedByTheJob)
{
    TempDirectory temp;
    for(const auto& name : {"project_2026_08_01 10_00_00", "project_2026_08_02 10_00_00",
                            "projectile_2026_07_01 10_00_00", "project_notes", "unrelated"})
        std::filesystem::create_directory(temp.path / name);

    EXPECT_TRUE(backup_core::RotateOwnedBackups(temp.path, "project", 2));
    EXPECT_FALSE(std::filesystem::exists(temp.path / "project_2026_08_01 10_00_00"));
    EXPECT_TRUE(std::filesystem::exists(temp.path / "project_2026_08_02 10_00_00"));
    EXPECT_TRUE(std::filesystem::exists(temp.path / "projectile_2026_07_01 10_00_00"));
    EXPECT_TRUE(std::filesystem::exists(temp.path / "project_notes"));
}

TEST_CASE(BackupRotationHonorsMaxBackupBoundaries)
{
    TempDirectory temp;
    std::filesystem::create_directory(temp.path / "source_2026_01_01 00_00_00");
    EXPECT_TRUE(backup_core::RotationCandidates(temp.path, "source", 0).empty());
    EXPECT_EQ(backup_core::RotationCandidates(temp.path, "source", 1).size(), size_t{1});
    EXPECT_TRUE(backup_core::RotationCandidates(temp.path, "source", 2).empty());
}

TEST_CASE(BackupSourceMetricsCountOnlyIncludedFiles)
{
    TempDirectory temp;
    WriteFile(temp.path / "source" / "one.bin", "1234");
    WriteFile(temp.path / "source" / "cache" / "ignored.bin", "12345678");
    WriteFile(temp.path / "source" / "nested" / "two.bin", "12");

    const auto metrics = backup_core::MeasureSource(temp.path / "source", {"cache"});
    EXPECT_TRUE(metrics.success);
    EXPECT_EQ(metrics.file_count, std::size_t{2});
    EXPECT_EQ(metrics.bytes, std::uintmax_t{6});
}

TEST_CASE(BackupCopyAppliesIgnoreRulesAndPreservesSymlinksWhenSupported)
{
    TempDirectory temp;
    const auto source = temp.path / "source";
    const auto destination = temp.path / "destination";
    WriteFile(source / "keep.txt", "keep");
    WriteFile(source / "cache" / "ignored.tmp", "ignore");

    std::error_code symlink_error;
    std::filesystem::create_symlink("keep.txt", source / "keep.link", symlink_error);
    backup_core::Options options;
    options.ignore_rules = {"cache"};
    const auto results = backup_core::CopyToDestinations(source, {destination}, "snapshot", options);

    EXPECT_EQ(results.size(), size_t{1});
    EXPECT_TRUE(results[0].success);
    EXPECT_TRUE(std::filesystem::exists(destination / "snapshot" / "keep.txt"));
    EXPECT_FALSE(std::filesystem::exists(destination / "snapshot" / "cache"));
    if(!symlink_error)
        EXPECT_TRUE(std::filesystem::is_symlink(destination / "snapshot" / "keep.link"));
}

TEST_CASE(BackupCancellationStopsHalfwayAndLeavesNothingBehind)
{
    TempDirectory temp;
    const auto source = temp.path / "source";
    for(int index = 0; index < 5; ++index)
        WriteFile(source / (std::to_string(index) + ".txt"), "content");

    std::atomic<bool> cancelled = false;
    int copied = 0;
    backup_core::Options options;
    options.cancelled = &cancelled;
    options.after_copy_file = [&](const auto&, const auto&) {
        if(++copied == 2) cancelled = true;
    };
    const auto results = backup_core::CopyToDestinations(source, {temp.path / "out"}, "snapshot", options);

    EXPECT_TRUE(results[0].cancelled);
    EXPECT_EQ(copied, 2);
    // A half-copied tree under a valid backup name would be counted by
    // rotation and could be restored from as though it were complete.
    EXPECT_FALSE(std::filesystem::exists(temp.path / "out" / "snapshot"));
    EXPECT_FALSE(std::filesystem::exists(temp.path / "out" / "snapshot.incomplete"));
}

TEST_CASE(BackupHashMismatchFailsTheDestination)
{
    TempDirectory temp;
    const auto source = temp.path / "source";
    WriteFile(source / "data.bin", "correct");

    backup_core::Options options;
    options.verify_hash = true;
    options.after_copy_file = [](const auto&, const auto& destination) { WriteFile(destination, "corrupt"); };
    const auto results = backup_core::CopyToDestinations(source, {temp.path / "out"}, "snapshot", options);
    EXPECT_TRUE(results[0].hash_mismatch);
    EXPECT_FALSE(results[0].success);
}

TEST_CASE(BackupCompressionFailureKeepsUncompressedDirectory)
{
    TempDirectory temp;
    const auto source = temp.path / "source";
    WriteFile(source / "data.bin", "content");

    backup_core::Options options;
    options.compressor = [](const auto&) { return false; };
    const auto results = backup_core::CopyToDestinations(source, {temp.path / "out"}, "snapshot", options);
    EXPECT_FALSE(results[0].success);
    EXPECT_TRUE(std::filesystem::exists(temp.path / "out" / "snapshot" / "data.bin"));
}

TEST_CASE(BackupContinuesWhenOneOfMultipleDestinationsFails)
{
    TempDirectory temp;
    const auto source = temp.path / "source";
    WriteFile(source / "data.bin", "content");
    const auto invalid_root = temp.path / "not_a_directory";
    WriteFile(invalid_root, "file");

    const auto results = backup_core::CopyToDestinations(source,
        {invalid_root, temp.path / "working"}, "snapshot");
    EXPECT_EQ(results.size(), size_t{2});
    EXPECT_FALSE(results[0].success);
    EXPECT_TRUE(results[1].success);
    EXPECT_TRUE(std::filesystem::exists(temp.path / "working" / "snapshot" / "data.bin"));
}

TEST_CASE(BackupReportsMissingOrInaccessibleSources)
{
    TempDirectory temp;
    const auto results = backup_core::CopyToDestinations(
        temp.path / "missing-source", {temp.path / "out"}, "snapshot");
    ASSERT_EQ(results.size(), size_t{1});
    EXPECT_FALSE(results[0].success);
    EXPECT_FALSE(results[0].error.empty());
}

/* --- Rotation, past the two-entry cases --- */

TEST_CASE(BackupRotationRemovesCompressedArchivesTheSameAsDirectories)
{
    // A compressed job leaves <source>_<timestamp>.7z files rather than
    // directories, and those are the entries rotation has to count.
    TempDirectory temp;
    for(const auto& name : {"project_2026_08_01 10_00_00.7z", "project_2026_08_02 10_00_00.7z"})
        WriteFile(temp.path / name, "archive");
    WriteFile(temp.path / "projectile_2026_07_01 10_00_00.7z", "someone else's");
    WriteFile(temp.path / "project_notes.7z", "not a backup");

    EXPECT_TRUE(backup_core::RotateOwnedBackups(temp.path, "project", 2));
    EXPECT_FALSE(std::filesystem::exists(temp.path / "project_2026_08_01 10_00_00.7z"));
    EXPECT_TRUE(std::filesystem::exists(temp.path / "project_2026_08_02 10_00_00.7z"));
    EXPECT_TRUE(std::filesystem::exists(temp.path / "projectile_2026_07_01 10_00_00.7z"));
    EXPECT_TRUE(std::filesystem::exists(temp.path / "project_notes.7z"));
}

TEST_CASE(BackupRotationRemovesTheOldestAndLeavesRoomForTheNewOne)
{
    TempDirectory temp;
    for(const auto& name : {"source_2026_01_01 00_00_00", "source_2026_02_01 00_00_00",
                            "source_2026_03_01 00_00_00", "source_2026_04_01 00_00_00",
                            "source_2026_05_01 00_00_00"})
        std::filesystem::create_directory(temp.path / name);

    // Three kept in the end means two survive the rotation that precedes the
    // new backup, so the three oldest go.
    const auto candidates = backup_core::RotationCandidates(temp.path, "source", 3);
    ASSERT_EQ(candidates.size(), size_t{3});
    EXPECT_EQ(candidates[0].filename().string(), std::string("source_2026_01_01 00_00_00"));
    EXPECT_EQ(candidates[2].filename().string(), std::string("source_2026_03_01 00_00_00"));

    EXPECT_TRUE(backup_core::RotateOwnedBackups(temp.path, "source", 3));
    EXPECT_FALSE(std::filesystem::exists(temp.path / "source_2026_03_01 00_00_00"));
    EXPECT_TRUE(std::filesystem::exists(temp.path / "source_2026_04_01 00_00_00"));
    EXPECT_TRUE(std::filesystem::exists(temp.path / "source_2026_05_01 00_00_00"));
}

/* --- Compression --- */

TEST_CASE(BackupCompressionSuccessReplacesTheCopyWithTheArchive)
{
    TempDirectory temp;
    const auto source = temp.path / "source";
    WriteFile(source / "data.bin", "content");

    std::filesystem::path compressed_path;
    backup_core::Options options;
    options.compressor = [&](const std::filesystem::path& directory) {
        compressed_path = directory;
        WriteFile(directory.string() + ".7z", "archive");
        return true;
    };
    const auto results = backup_core::CopyToDestinations(source, {temp.path / "out"}, "snapshot", options);

    ASSERT_EQ(results.size(), size_t{1});
    EXPECT_TRUE(results[0].success);
    EXPECT_TRUE(results[0].compressed);
    EXPECT_EQ(compressed_path, temp.path / "out" / "snapshot");
    // the uncompressed copy is removed once the archive exists
    EXPECT_FALSE(std::filesystem::exists(temp.path / "out" / "snapshot"));
    EXPECT_TRUE(std::filesystem::exists(temp.path / "out" / "snapshot.7z"));
}

/* --- The hooks the application hangs progress and attributes on --- */

TEST_CASE(BackupReportsEveryDirectoryItCreates)
{
    TempDirectory temp;
    const auto source = temp.path / "source";
    WriteFile(source / "nested" / "deeper" / "file.txt", "content");

    std::vector<std::string> created;
    backup_core::Options options;
    options.after_create_directory = [&](const std::filesystem::path& from,
                                         const std::filesystem::path& to) {
        EXPECT_EQ(from.filename(), to.filename());
        created.push_back(to.filename().string());
    };
    const auto results = backup_core::CopyToDestinations(source, {temp.path / "out"}, "snapshot", options);

    EXPECT_TRUE(results[0].success);
    ASSERT_EQ(created.size(), size_t{2});
    EXPECT_EQ(created[0], std::string("nested"));
    EXPECT_EQ(created[1], std::string("deeper"));
}

/* --- Hashing --- */

TEST_CASE(BackupHashingReadsFilesLargerThanItsBuffer)
{
    TempDirectory temp;
    const auto source = temp.path / "source";
    WriteFile(source / "big.bin", std::string(200 * 1024, 'x'));

    backup_core::Options options;
    options.verify_hash = true;
    options.hash_buffer_bytes = 4096;
    const auto results = backup_core::CopyToDestinations(source, {temp.path / "out"}, "snapshot", options);

    ASSERT_EQ(results.size(), size_t{1});
    // a faithful copy must survive a multi-chunk read
    EXPECT_TRUE(results[0].success);
    EXPECT_FALSE(results[0].hash_mismatch);
}

TEST_CASE(BackupHashingCatchesCorruptionPastTheFirstChunk)
{
    TempDirectory temp;
    const auto source = temp.path / "source";
    WriteFile(source / "big.bin", std::string(200 * 1024, 'x'));

    backup_core::Options options;
    options.verify_hash = true;
    options.hash_buffer_bytes = 4096;
    options.after_copy_file = [](const auto&, const std::filesystem::path& destination) {
        // Differs only well past the first buffer-full.
        WriteFile(destination, std::string(100 * 1024, 'x') + std::string(100 * 1024, 'y'));
    };
    const auto results = backup_core::CopyToDestinations(source, {temp.path / "out"}, "snapshot", options);

    EXPECT_TRUE(results[0].hash_mismatch);
    EXPECT_FALSE(results[0].success);
}

TEST_CASE(BackupHashingWithoutABufferSizeStillWorks)
{
    TempDirectory temp;
    const auto source = temp.path / "source";
    WriteFile(source / "big.bin", std::string(200 * 1024, 'z'));

    backup_core::Options options;
    options.verify_hash = true;
    options.hash_buffer_bytes = 0;  /* falls back to the built-in size */
    const auto results = backup_core::CopyToDestinations(source, {temp.path / "out"}, "snapshot", options);

    EXPECT_TRUE(results[0].success);
}

/* --- Boundaries --- */

TEST_CASE(BackupCancelledBeforeItStartsCreatesNothing)
{
    TempDirectory temp;
    const auto source = temp.path / "source";
    WriteFile(source / "file.txt", "content");

    std::atomic<bool> cancelled = true;
    backup_core::Options options;
    options.cancelled = &cancelled;
    const auto results = backup_core::CopyToDestinations(source,
        {temp.path / "first", temp.path / "second"}, "snapshot", options);

    ASSERT_EQ(results.size(), size_t{2});
    EXPECT_TRUE(results[0].cancelled);
    EXPECT_TRUE(results[1].cancelled);
    // a backup cancelled before it starts must not leave a directory behind
    EXPECT_FALSE(std::filesystem::exists(temp.path / "first"));
    EXPECT_FALSE(std::filesystem::exists(temp.path / "second"));
}

TEST_CASE(BackupIgnoreRulesMatchAnywhereInTheRelativePath)
{
    // The rule is an unanchored substring of the whole relative path, not a
    // path component and not a glob. Pinned because settings files are written
    // against this, not because it is the obvious design.
    TempDirectory temp;
    const auto source = temp.path / "source";
    WriteFile(source / "build" / "a.o", "x");
    WriteFile(source / "src" / "rebuild" / "b.cpp", "x");
    WriteFile(source / "prebuild.txt", "x");
    WriteFile(source / "keep.txt", "x");

    backup_core::Options options;
    options.ignore_rules = {"build"};
    const auto results = backup_core::CopyToDestinations(source, {temp.path / "out"}, "snapshot", options);

    const auto backup = temp.path / "out" / "snapshot";
    EXPECT_TRUE(results[0].success);
    EXPECT_TRUE(std::filesystem::exists(backup / "keep.txt"));
    EXPECT_FALSE(std::filesystem::exists(backup / "build"));
    // a rule matches in the middle of a path too
    EXPECT_FALSE(std::filesystem::exists(backup / "src" / "rebuild"));
    // and in the middle of a file name
    EXPECT_FALSE(std::filesystem::exists(backup / "prebuild.txt"));
}

TEST_CASE(BackupOfAnEmptySourceProducesAnEmptyBackup)
{
    TempDirectory temp;
    const auto source = temp.path / "source";
    std::filesystem::create_directories(source);

    const auto results = backup_core::CopyToDestinations(source, {temp.path / "out"}, "snapshot");

    ASSERT_EQ(results.size(), size_t{1});
    EXPECT_TRUE(results[0].success);
    EXPECT_TRUE(std::filesystem::is_directory(temp.path / "out" / "snapshot"));
}

TEST_CASE(BackupWithNoDestinationsReportsNothing)
{
    TempDirectory temp;
    const auto source = temp.path / "source";
    WriteFile(source / "file.txt", "content");

    EXPECT_TRUE(backup_core::CopyToDestinations(source, {}, "snapshot").empty());
}

TEST_CASE(BackupRefusesADestinationInsideTheSource)
{
    // Walking a destination that lives under the source copies the backup into
    // itself until the path length or the disk runs out.
    TempDirectory temp;
    const auto source = temp.path / "source";
    WriteFile(source / "file.txt", "content");

    const auto results = backup_core::CopyToDestinations(source,
        {source / "backups", temp.path / "outside"}, "snapshot");

    ASSERT_EQ(results.size(), size_t{2});
    EXPECT_FALSE(results[0].success);
    EXPECT_FALSE(results[0].error.empty());
    EXPECT_FALSE(std::filesystem::exists(source / "backups"));
    // a bad destination must not stop the good one
    EXPECT_TRUE(results[1].success);
}

/* --- Rotation follows the configured name format --- */

TEST_CASE(BackupTimeFormatsThatCannotBeReadBackAreRejected)
{
    EXPECT_TRUE(backup_core::IsRotatableTimeFormat(backup_core::default_time_format));
    EXPECT_TRUE(backup_core::IsRotatableTimeFormat("_%Y%m%d"));
    EXPECT_TRUE(backup_core::IsRotatableTimeFormat("-%y-%j"));

    // Nothing to tell one backup from the next.
    EXPECT_FALSE(backup_core::IsRotatableTimeFormat(""));
    EXPECT_FALSE(backup_core::IsRotatableTimeFormat("_backup"));
    EXPECT_FALSE(backup_core::IsRotatableTimeFormat("_100%%"));
    // Specifiers of no fixed width cannot be matched back.
    EXPECT_FALSE(backup_core::IsRotatableTimeFormat("_%Y_%B"));
    EXPECT_FALSE(backup_core::IsRotatableTimeFormat("_%Y_%"));
}

TEST_CASE(BackupNamesAreRecognisedUnderTheFormatThatProducedThem)
{
    EXPECT_TRUE(backup_core::IsOwnedBackupName("project", "project_20260801", "_%Y%m%d"));
    EXPECT_TRUE(backup_core::IsOwnedBackupName("project", "project_20260801.7z", "_%Y%m%d"));

    // The default shape is not accepted by a different format, and vice versa.
    EXPECT_FALSE(backup_core::IsOwnedBackupName("project", "project_2026_08_01 10_00_00", "_%Y%m%d"));
    EXPECT_FALSE(backup_core::IsOwnedBackupName("project", "project_20260801"));

    // Nothing is owned when the format cannot be read back at all.
    EXPECT_FALSE(backup_core::IsOwnedBackupName("project", "project", ""));
    EXPECT_FALSE(backup_core::IsOwnedBackupName("project", "project_anything", "_backup"));
}

TEST_CASE(BackupRotationUsesTheConfiguredFormatRatherThanAFixedShape)
{
    TempDirectory temp;
    for(const auto& name : {"source_20260101", "source_20260201", "source_20260301"})
        std::filesystem::create_directory(temp.path / name);
    std::filesystem::create_directory(temp.path / "source_2026_04_01 00_00_00");

    EXPECT_TRUE(backup_core::RotateOwnedBackups(temp.path, "source", 2, "_%Y%m%d"));
    EXPECT_FALSE(std::filesystem::exists(temp.path / "source_20260101"));
    EXPECT_FALSE(std::filesystem::exists(temp.path / "source_20260201"));
    EXPECT_TRUE(std::filesystem::exists(temp.path / "source_20260301"));
    // Named by a different format, so not this job's to delete.
    EXPECT_TRUE(std::filesystem::exists(temp.path / "source_2026_04_01 00_00_00"));
}

TEST_CASE(BackupRotationKeepsEverythingWhenTheFormatCannotBeReadBack)
{
    TempDirectory temp;
    for(const auto& name : {"source_2026_01_01 00_00_00", "source_2026_02_01 00_00_00"})
        std::filesystem::create_directory(temp.path / name);

    // Better to keep too much than to delete something that only looks like
    // one of ours.
    EXPECT_TRUE(backup_core::RotationCandidates(temp.path, "source", 1, "").empty());
    EXPECT_TRUE(backup_core::RotateOwnedBackups(temp.path, "source", 1, ""));
    EXPECT_TRUE(std::filesystem::exists(temp.path / "source_2026_01_01 00_00_00"));
    EXPECT_TRUE(std::filesystem::exists(temp.path / "source_2026_02_01 00_00_00"));
}

TEST_CASE(BackupRefusesToOverwriteABackupOfTheSameName)
{
    // What a time format with no time in it produces on the second run.
    TempDirectory temp;
    const auto source = temp.path / "source";
    WriteFile(source / "file.txt", "content");

    const auto first = backup_core::CopyToDestinations(source, {temp.path / "out"}, "snapshot");
    EXPECT_TRUE(first[0].success);

    const auto second = backup_core::CopyToDestinations(source, {temp.path / "out"}, "snapshot");
    ASSERT_EQ(second.size(), size_t{1});
    EXPECT_FALSE(second[0].success);
    EXPECT_FALSE(second[0].error.empty());
    // The backup that was already there is untouched.
    EXPECT_TRUE(std::filesystem::exists(temp.path / "out" / "snapshot" / "file.txt"));
}

/* --- Partial backups --- */

TEST_CASE(BackupAppearsUnderItsFinalNameOnlyOnceItIsComplete)
{
    TempDirectory temp;
    const auto source = temp.path / "source";
    for(int index = 0; index < 4; ++index)
        WriteFile(source / (std::to_string(index) + ".txt"), "content");

    bool seen_final_name_early = false;
    backup_core::Options options;
    options.after_copy_file = [&](const auto&, const auto&) {
        if(std::filesystem::exists(temp.path / "out" / "snapshot"))
            seen_final_name_early = true;
    };
    const auto results = backup_core::CopyToDestinations(source, {temp.path / "out"}, "snapshot", options);

    EXPECT_TRUE(results[0].success);
    EXPECT_FALSE(seen_final_name_early);
    EXPECT_TRUE(std::filesystem::exists(temp.path / "out" / "snapshot" / "0.txt"));
    EXPECT_FALSE(std::filesystem::exists(temp.path / "out" / "snapshot.incomplete"));
}

TEST_CASE(BackupWithACorruptCopyIsNotLeftLookingLikeABackup)
{
    TempDirectory temp;
    const auto source = temp.path / "source";
    WriteFile(source / "data.bin", "correct");

    backup_core::Options options;
    options.verify_hash = true;
    options.after_copy_file = [](const auto&, const auto& destination) { WriteFile(destination, "corrupt"); };
    const auto results = backup_core::CopyToDestinations(source, {temp.path / "out"}, "snapshot", options);

    EXPECT_TRUE(results[0].hash_mismatch);
    EXPECT_FALSE(std::filesystem::exists(temp.path / "out" / "snapshot"));
}

TEST_CASE(BackupRotationSweepsPartialsLeftByARunThatNeverFinished)
{
    // A process killed mid-copy leaves one of these. It is not a backup, so it
    // must neither be counted towards the limit nor kept.
    TempDirectory temp;
    std::filesystem::create_directory(temp.path / "source_2026_01_01 00_00_00");
    std::filesystem::create_directory(temp.path / "source_2026_02_01 00_00_00.incomplete");
    std::filesystem::create_directory(temp.path / "unrelated.incomplete");

    EXPECT_TRUE(backup_core::RotateOwnedBackups(temp.path, "source", 5));
    EXPECT_FALSE(std::filesystem::exists(temp.path / "source_2026_02_01 00_00_00.incomplete"));
    EXPECT_TRUE(std::filesystem::exists(temp.path / "source_2026_01_01 00_00_00"));
    EXPECT_TRUE(std::filesystem::exists(temp.path / "unrelated.incomplete"));
}

TEST_CASE(BackupRotationDoesNotCountPartialsTowardsTheLimit)
{
    TempDirectory temp;
    std::filesystem::create_directory(temp.path / "source_2026_01_01 00_00_00");
    std::filesystem::create_directory(temp.path / "source_2026_02_01 00_00_00.incomplete");

    // One real backup and a limit of two leaves room for the new one, so
    // nothing may be rotated away.
    EXPECT_TRUE(backup_core::RotationCandidates(temp.path, "source", 2).empty());
}

TEST_CASE(BackupRefusesTheSourceItselfAsADestination)
{
    TempDirectory temp;
    const auto source = temp.path / "source";
    WriteFile(source / "file.txt", "content");

    const auto results = backup_core::CopyToDestinations(source, {source}, "snapshot");
    ASSERT_EQ(results.size(), size_t{1});
    EXPECT_FALSE(results[0].success);
}
