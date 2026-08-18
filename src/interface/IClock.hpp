#pragma once

#include <chrono>

class IClock
{
public:
    using TimePoint = std::chrono::steady_clock::time_point;

    virtual ~IClock() = default;

    [[nodiscard]] virtual TimePoint Now() const noexcept = 0;
};
