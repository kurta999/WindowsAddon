#pragma once

#include <cstdint>
#include <span>
#include <string>
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

// The name a session carries after the midnight rollover splits it: the
// previous day's entry is closed at the day boundary and this continues it.
// The rule lived inline in the rollover branch of the panel's minute tick.
[[nodiscard]] std::string ContinuationName(const std::string& comment);

// The TotalWork column label: hours worked and what they pay, both to two
// decimals. There were two independent computations of this label - one per
// row added, truncating to whole hours (59 minutes paid nothing), one at
// startup, keeping fractions - and whichever ran last was what the user saw.
// Fractions win: no pay is lost to rounding. Negative time reads as zero,
// which the truncating copy already ensured.
[[nodiscard]] std::string FormatTotalWorkLabel(std::int64_t total_seconds, int hourly_rate);
}
