#include "TestFramework.hpp"

#include "TimeTrackerStorage.hpp"
#include "TimeTrackerLogic.hpp"

#include <cstdint>
#include <chrono>
#include <string>
#include <vector>

namespace
{
constexpr std::int64_t March31_2024_0030Utc = 1711845000;
}

TEST_CASE(TimeTrackerCommentsRoundTripThroughPreparedStatements)
{
    TimeTrackerStorage storage(":memory:");
    const std::string comment = "O'Brien — Prüfung 🚗'); DROP TABLE time_table; --";

    const auto id = storage.Insert(100, 200, comment);
    EXPECT_TRUE(id.has_value());

    const auto rows = storage.LoadRange(0, 1000);
    EXPECT_EQ(rows.size(), size_t{1});
    EXPECT_EQ(rows[0].comment, comment);

    EXPECT_TRUE(storage.Insert(300, 400, "still present").has_value());
    EXPECT_EQ(storage.LoadRange(0, 1000).size(), size_t{2});
}

TEST_CASE(TimeTrackerInsertEditDeleteAndReload)
{
    TimeTrackerStorage storage(":memory:");
    const auto id = storage.Insert(1000, 1600, "initial");
    EXPECT_TRUE(id.has_value());
    EXPECT_TRUE(storage.Update(*id, 1100, 1800, "edited ü"));

    auto rows = storage.LoadRange(0, 2000);
    EXPECT_EQ(rows.size(), size_t{1});
    EXPECT_EQ(rows[0].start, std::int64_t{1100});
    EXPECT_EQ(rows[0].end, std::int64_t{1800});
    EXPECT_EQ(rows[0].comment, std::string("edited ü"));

    EXPECT_TRUE(storage.Remove(*id));
    EXPECT_TRUE(storage.LoadRange(0, 2000).empty());
    EXPECT_FALSE(storage.Remove(*id));
}

TEST_CASE(TimeTrackerStoresOverlapsWithoutLosingEitherEntry)
{
    TimeTrackerStorage storage(":memory:");
    EXPECT_TRUE(storage.Insert(100, 300, "first").has_value());
    EXPECT_TRUE(storage.Insert(200, 400, "overlap").has_value());

    const auto rows = storage.LoadRange(0, 500);
    EXPECT_EQ(rows.size(), size_t{2});
    EXPECT_EQ(rows[0].comment, std::string("first"));
    EXPECT_EQ(rows[1].comment, std::string("overlap"));
}

TEST_CASE(TimeTrackerDetectsOverlapsAcrossMidnight)
{
    const std::vector<time_tracker_logic::Interval> intervals{
        {23 * 3600, 25 * 3600},       // 23:00 -> 01:00 next day
        {24 * 3600 + 30 * 60, 26 * 3600},
        {26 * 3600, 27 * 3600}        // touches, but does not overlap
    };
    const auto overlap = time_tracker_logic::DetectOverlaps(intervals);
    EXPECT_TRUE(overlap[0]);
    EXPECT_TRUE(overlap[1]);
    EXPECT_FALSE(overlap[2]);
}

TEST_CASE(TimeTrackerOverlapDetectionHandlesNestedAndInvalidEntries)
{
    const std::vector<time_tracker_logic::Interval> intervals{
        {100, 500}, {200, 300}, {400, 600}, {700, 600}, {800, 800}
    };
    const auto overlap = time_tracker_logic::DetectOverlaps(intervals);
    EXPECT_TRUE(overlap[0]);
    EXPECT_TRUE(overlap[1]);
    EXPECT_TRUE(overlap[2]);
    EXPECT_FALSE(overlap[3]);
    EXPECT_FALSE(overlap[4]);
}

TEST_CASE(TimeTrackerMinutePersistenceGateOnlyFiresOncePerMinute)
{
    EXPECT_FALSE(time_tracker_logic::ShouldPersistElapsedMinute(0, 0));
    EXPECT_FALSE(time_tracker_logic::ShouldPersistElapsedMinute(59, 0));
    EXPECT_TRUE(time_tracker_logic::ShouldPersistElapsedMinute(60, 0));
    EXPECT_FALSE(time_tracker_logic::ShouldPersistElapsedMinute(61, 1));
    EXPECT_FALSE(time_tracker_logic::ShouldPersistElapsedMinute(119, 1));
    EXPECT_TRUE(time_tracker_logic::ShouldPersistElapsedMinute(120, 1));
    EXPECT_FALSE(time_tracker_logic::ShouldPersistElapsedMinute(-1, 0));
}

TEST_CASE(TimeTrackerHandlesMidnightMonthLeapYearAndDstInstants)
{
    TimeTrackerStorage storage(":memory:");
    // 2024-02-29 23:30 -> 2024-03-01 00:30 UTC (leap day/month boundary).
    EXPECT_TRUE(storage.Insert(1709249400, 1709253000, "leap midnight").has_value());
    // Europe/Berlin's 2024 spring-forward instant, stored as UTC epoch seconds.
    EXPECT_TRUE(storage.Insert(March31_2024_0030Utc, March31_2024_0030Utc + 7200, "DST transition").has_value());

    const auto leap = storage.LoadRange(1709240000, 1709250000);
    EXPECT_EQ(leap.size(), size_t{1});
    EXPECT_EQ(leap[0].end - leap[0].start, std::int64_t{3600});

    const auto dst = storage.LoadRange(March31_2024_0030Utc, March31_2024_0030Utc);
    EXPECT_EQ(dst.size(), size_t{1});
    EXPECT_EQ(dst[0].end - dst[0].start, std::int64_t{7200});
}

#if defined(__cpp_lib_chrono) && __cpp_lib_chrono >= 201907L
TEST_CASE(TimeTrackerDstTransitionUsesElapsedTimeRatherThanWallClockDifference)
{
    using namespace std::chrono;
    const auto* berlin = locate_zone("Europe/Berlin");
    const local_seconds before = local_days{2024y / March / 31} + 1h + 30min;
    const local_seconds after = local_days{2024y / March / 31} + 3h + 30min;

    EXPECT_EQ(berlin->to_sys(after) - berlin->to_sys(before), 1h);
    const local_seconds nonexistent = local_days{2024y / March / 31} + 2h + 30min;
    EXPECT_THROW((void)berlin->to_sys(nonexistent), nonexistent_local_time);
}
#endif

TEST_CASE(TimeTrackerFailedTransactionRollsBackAllRows)
{
    TimeTrackerStorage storage(":memory:");
    EXPECT_TRUE(storage.Insert(10, 20, "original").has_value());

    const std::vector<StoredTimeEntry> replacement{
        {100, 30, 40, "valid"},
        {100, 80, 90, "duplicate primary key"}
    };
    EXPECT_FALSE(storage.ReplaceAll(replacement));

    const auto rows = storage.LoadRange(0, 1000);
    EXPECT_EQ(rows.size(), size_t{1});
    EXPECT_EQ(rows[0].comment, std::string("original"));
}
