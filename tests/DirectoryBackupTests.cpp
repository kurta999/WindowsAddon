#include "TestFramework.hpp"

#include "DirectoryBackupCore.hpp"

#include <atomic>
#include <filesystem>
#include <fstream>
#include <string>

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

TEST_CASE(BackupCancellationStopsHalfwayAndLeavesPartialCopy)
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
