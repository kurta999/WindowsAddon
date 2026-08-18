#include "TimeTrackerLogic.hpp"

#include <algorithm>
#include <numeric>

namespace time_tracker_logic
{
std::vector<bool> DetectOverlaps(std::span<const Interval> intervals)
{
    std::vector<bool> overlaps(intervals.size(), false);
    std::vector<std::size_t> order(intervals.size());
    std::iota(order.begin(), order.end(), std::size_t{0});
    std::ranges::sort(order, {}, [&](std::size_t index) { return intervals[index].start; });

    for(std::size_t left = 0; left < order.size(); ++left)
    {
        const auto left_index = order[left];
        const auto& first = intervals[left_index];
        if(first.end <= first.start) continue;
        for(std::size_t right = left + 1; right < order.size(); ++right)
        {
            const auto right_index = order[right];
            const auto& second = intervals[right_index];
            if(second.start >= first.end) break;
            if(second.end > second.start && second.end > first.start)
            {
                overlaps[left_index] = true;
                overlaps[right_index] = true;
            }
        }
    }
    return overlaps;
}

bool ShouldPersistElapsedMinute(std::int64_t elapsed_seconds, std::int64_t last_persisted_minute)
{
    if(elapsed_seconds < 0)
        return false;
    return elapsed_seconds / 60 > last_persisted_minute;
}
}
