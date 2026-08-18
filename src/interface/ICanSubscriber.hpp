#pragma once

#include "ICanObserver.hpp"

#include <algorithm>
#include <mutex>
#include <vector>

class ICanSubscriber
{
public:
    ICanSubscriber() = default;
    virtual ~ICanSubscriber() = default;

    void RegisterObserver(ICanObserver* observer)
    {
        if(!observer)
            return;

        std::scoped_lock lock(m_ObserversMutex);
        if(std::ranges::find(m_Observers, observer) == m_Observers.end())
            m_Observers.push_back(observer);
    }

    void UnregisterObserver(ICanObserver* observer)
    {
        std::scoped_lock lock(m_ObserversMutex);
        std::erase(m_Observers, observer);
    }

protected:
    void NotifyFrameOnBus(uint32_t frame_id, uint8_t* data, uint16_t size) const
    {
        for(auto* observer : ObserverSnapshot())
            observer->OnFrameOnBus(frame_id, data, size);
    }

    void NotifyIsoTpData(uint32_t frame_id, uint8_t* data, uint16_t size) const
    {
        for(auto* observer : ObserverSnapshot())
            observer->OnIsoTpDataReceived(frame_id, data, size);
    }

private:
    [[nodiscard]] std::vector<ICanObserver*> ObserverSnapshot() const
    {
        std::scoped_lock lock(m_ObserversMutex);
        return m_Observers;
    }

    // Observers are non-owning and must unregister before destruction. A
    // snapshot permits callbacks to unregister themselves without invalidating
    // the active notification pass or deadlocking on the registry mutex.
    mutable std::mutex m_ObserversMutex;
    std::vector<ICanObserver*> m_Observers;
};
