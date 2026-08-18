#pragma once

#include "interface/IClock.hpp"

class SystemClock final : public IClock
{
public:
    [[nodiscard]] TimePoint Now() const noexcept override;
};
