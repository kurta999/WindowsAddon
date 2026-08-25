#pragma once

#include <functional>
#include <utility>
#include <vector>

// !\brief What the main frame drives on each timer tick.
//
// The frame used to name every panel twice: once to construct it and add its
// notebook page, and again inside the timer handler as
// `if(can_panel) can_panel->On10MsTimer();`. Those two lists were maintained by
// hand, so a new panel that forgot the second one simply never ticked, and the
// frame had to know about every feature in the application to run its clock.
//
// A tick is a closure registered at construction, so the two lists became one.
namespace gui
{
class TickRegistry
{
public:
    void Register(std::function<void()> tick)
    {
        if(tick)
            m_ticks.push_back(std::move(tick));
    }

    // !\brief Run every registered tick, in registration order.
    void TickAll() const
    {
        for(const auto& tick : m_ticks)
            tick();
    }

    void Clear() { m_ticks.clear(); }

    [[nodiscard]] size_t Count() const { return m_ticks.size(); }

private:
    std::vector<std::function<void()>> m_ticks;
};
}
