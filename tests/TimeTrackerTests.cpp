#include "TestFramework.hpp"

#include "TimeTracker.hpp"

#include <algorithm>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <boost/date_time/gregorian/gregorian.hpp>
#include <boost/date_time/posix_time/posix_time.hpp>

namespace
{
class FakeTimeTrackerStorage final : public ITimeTrackerStorage
{
public:
    bool IsOpen() const override { return open; }
    const std::string& LastError() const override { return error; }

    std::optional<int> Insert(
        std::int64_t start, std::int64_t end, const std::string& comment) override
    {
        if(!open || fail_insert)
        {
            error = "injected insert failure";
            return std::nullopt;
        }
        rows.push_back({next_id, start, end, comment});
        return next_id++;
    }

    bool Update(
        int id, std::int64_t start, std::int64_t end, const std::string& comment) override
    {
        const auto row = std::ranges::find(rows, id, &StoredTimeEntry::id);
        if(row == rows.end())
        {
            error = "missing row";
            return false;
        }
        row->start = start;
        row->end = end;
        row->comment = comment;
        return true;
    }

    bool Remove(int id) override
    {
        const auto row = std::ranges::find(rows, id, &StoredTimeEntry::id);
        if(row == rows.end())
        {
            error = "missing row";
            return false;
        }
        rows.erase(row);
        return true;
    }

    std::vector<StoredTimeEntry> LoadRange(
        std::int64_t first_start, std::int64_t last_start) override
    {
        std::vector<StoredTimeEntry> result;
        for(const auto& row : rows)
        {
            if(row.start >= first_start && row.start <= last_start)
                result.push_back(row);
        }
        return result;
    }

    bool open{true};
    bool fail_insert{false};
    int next_id{1};
    std::string error;
    std::vector<StoredTimeEntry> rows;
};

boost::posix_time::ptime At(int year, int month, int day, int hour, int minute = 0)
{
    return {boost::gregorian::date(static_cast<unsigned short>(year),
                static_cast<unsigned short>(month), static_cast<unsigned short>(day)),
        boost::posix_time::hours(hour) + boost::posix_time::minutes(minute)};
}

std::int64_t SinceEpoch(const boost::posix_time::ptime& value)
{
    return (value - boost::posix_time::ptime(boost::gregorian::date(1970, 1, 1))).total_seconds();
}
}

TEST_CASE(TimeTrackerPersistsTheEndPassedWhenAddingAnEntry)
{
    auto storage = std::make_unique<FakeTimeTrackerStorage>();
    auto* storage_view = storage.get();
    TimeTracker tracker(std::move(storage));

    TimeEntry* entry = tracker.AddEntry(At(2026, 8, 17, 9), At(2026, 8, 17, 10), "manual row");
    ASSERT_TRUE(entry != nullptr);
    ASSERT_EQ(storage_view->rows.size(), size_t{1});
    EXPECT_EQ(storage_view->rows[0].end - storage_view->rows[0].start, std::int64_t{3600});
}

TEST_CASE(TimeTrackerQueuedOperationsAfterDeleteAreSafe)
{
    auto storage = std::make_unique<FakeTimeTrackerStorage>();
    TimeTracker tracker(std::move(storage));
    TimeEntry* entry = tracker.AddEntry(At(2026, 8, 17, 9), At(2026, 8, 17, 10), "delete me");
    ASSERT_TRUE(entry != nullptr);
    const int durable_id = entry->sql_id;

    EXPECT_TRUE(tracker.RemoveEntry(durable_id));
    EXPECT_TRUE(tracker.FindEntry(durable_id) == nullptr);
    EXPECT_FALSE(tracker.SaveEntry(durable_id));
    EXPECT_FALSE(tracker.EditEntry(durable_id, At(2026, 8, 17, 8), At(2026, 8, 17, 9), "late edit"));
    EXPECT_FALSE(tracker.RemoveEntry(durable_id));
}

TEST_CASE(TimeTrackerDateEditMovesOwnershipToTheCorrectMonth)
{
    auto storage = std::make_unique<FakeTimeTrackerStorage>();
    TimeTracker tracker(std::move(storage));
    TimeEntry* entry = tracker.AddEntry(At(2026, 1, 31, 23), At(2026, 2, 1, 0), "month end");
    ASSERT_TRUE(entry != nullptr);
    const int durable_id = entry->sql_id;

    EXPECT_TRUE(tracker.EditEntry(
        durable_id, At(2026, 2, 2, 23), At(2026, 2, 3, 0), "moved"));
    EXPECT_TRUE(tracker.FindEntry(durable_id) != nullptr);
    EXPECT_FALSE(tracker.GetEntries().contains(202601));
    EXPECT_TRUE(tracker.GetEntries().contains(202602));
    EXPECT_TRUE(tracker.RemoveEntry(durable_id));
}

TEST_CASE(TimeTrackerStableIdSurvivesModelReload)
{
    auto storage = std::make_unique<FakeTimeTrackerStorage>();
    auto* storage_view = storage.get();
    const auto inserted = storage_view->Insert(
        SinceEpoch(At(2026, 8, 17, 9)), SinceEpoch(At(2026, 8, 17, 10)), "loaded");
    ASSERT_TRUE(inserted.has_value());

    TimeTracker tracker(std::move(storage));
    tracker.SetActualYearMonth(2026, 8);
    tracker.LoadEntries(2026, 8);
    ASSERT_TRUE(tracker.FindEntry(*inserted) != nullptr);

    tracker.LoadEntries(2026, 8);
    EXPECT_TRUE(tracker.EditEntry(
        *inserted, At(2026, 8, 17, 9, 30), At(2026, 8, 17, 10, 30), "after reload"));
    EXPECT_EQ(storage_view->rows[0].comment, std::string("after reload"));
}

TEST_CASE(TimeTrackerAddFailureReturnsNullWithoutCreatingModelState)
{
    auto storage = std::make_unique<FakeTimeTrackerStorage>();
    storage->fail_insert = true;
    TimeTracker tracker(std::move(storage));

    EXPECT_TRUE(tracker.AddEntry(At(2026, 8, 17, 9), At(2026, 8, 17, 9), "") == nullptr);
    EXPECT_TRUE(tracker.GetEntries().empty());
    EXPECT_FALSE(tracker.LastError().empty());
}
