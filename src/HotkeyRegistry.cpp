#include "HotkeyRegistry.hpp"

#include <utility>

void HotkeyRegistry::SetUiMarshaller(UiMarshaller marshaller)
{
    std::scoped_lock lock(m_Mutex);
    m_Marshaller = std::move(marshaller);
}

void HotkeyRegistry::Register(IHotkeyHandler& handler)
{
    std::scoped_lock lock(m_Mutex);
    m_Handlers.push_back(&handler);
}

void HotkeyRegistry::Clear()
{
    std::scoped_lock lock(m_Mutex);
    m_Handlers.clear();
}

bool HotkeyRegistry::Dispatch(const std::string& key)
{
    if(key.empty())
        return false;

    IHotkeyHandler* matched = nullptr;
    UiMarshaller marshaller;
    {
        std::scoped_lock lock(m_Mutex);
        for(IHotkeyHandler* handler : m_Handlers)
        {
            if(!handler->IsHotkeyEnabled() || handler->HotkeyBinding() != key)
                continue;
            matched = handler;
            break;
        }
        marshaller = m_Marshaller;
    }

    if(!matched)
        return false;

    /* The action runs outside the lock: it can be long, and a UI-thread action
       reaches code that may register or query hotkeys itself. */
    if(matched->HotkeyNeedsUiThread() && marshaller)
        marshaller([matched] { matched->OnHotkeyPressed(); });
    else
        matched->OnHotkeyPressed();
    return true;
}

std::string_view HotkeyRegistry::OwnerOfBinding(const std::string& key) const
{
    if(key.empty())
        return {};

    std::scoped_lock lock(m_Mutex);
    for(const IHotkeyHandler* handler : m_Handlers)
    {
        /* A disabled feature still owns its key - rebinding a macro onto it
           would start conflicting the moment the feature is switched back on. */
        if(handler->HotkeyBinding() == key)
            return handler->HotkeyOwner();
    }
    return {};
}
