#pragma once

#include "ICanObserver.hpp"
#include <list>

class ICanSubscriber
{
public:
    ICanSubscriber() = default;
    virtual ~ICanSubscriber() = default;

    void RegisterObserver(ICanObserver* observer)
    {
        m_Observers.push_back(observer);
    }

    void UnregisterObserver(ICanObserver* observer)
    {
        m_Observers.remove(observer);
    }

protected:
    void NotifyFrameOnBus(uint32_t frame_id, uint8_t* data, uint16_t size) const
    {
        for(auto observer : m_Observers)
            observer->OnFrameOnBus(frame_id, data, size);
    }

    void NotifyIsoTpData(uint32_t frame_id, uint8_t* data, uint16_t size) const
    {
        for(auto observer : m_Observers)
            observer->OnIsoTpDataReceived(frame_id, data, size);
    }

    std::list<ICanObserver*> m_Observers;
};
