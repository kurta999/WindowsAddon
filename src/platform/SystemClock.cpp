#include "SystemClock.hpp"

IClock::TimePoint SystemClock::Now() const noexcept
{
    return std::chrono::steady_clock::now();
}
