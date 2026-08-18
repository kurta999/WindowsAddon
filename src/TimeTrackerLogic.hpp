#pragma once

#include <cstdint>
#include <span>
#include <vector>

namespace time_tracker_logic
{
struct Interval
{
    std::int64_t start{};
    std::int64_t end{};
};

// Returns one flag per input interval. End points that merely touch are not
// overlaps; invalid/zero-duration intervals are ignored.
[[nodiscard]] std::vector<bool> DetectOverlaps(std::span<const Interval> intervals);

// The GUI ticks many times per second. This gate ensures that persistence and
// grid rebuilds happen at most once for each newly completed minute.
[[nodiscard]] bool ShouldPersistElapsedMinute(
    std::int64_t elapsed_seconds, std::int64_t last_persisted_minute);
}
