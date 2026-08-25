#pragma once

#include "interface/IHotkeyHandler.hpp"

#include <functional>
#include <mutex>
#include <string>
#include <string_view>
#include <vector>

// !\brief The global hotkey chain: one ordered list of handlers, asked in turn.
//
// Chain of Responsibility. The first handler whose binding matches consumes the
// press and the walk stops, which is what the old if-chain did by falling
// through a series of early returns. Because the same list answers
// OwnerOfBinding, "which keys are reserved" can no longer drift away from
// "which keys do something".
class HotkeyRegistry
{
public:
    // !\brief How a UI-thread action is scheduled.
    // Set by the composition root; without one, such handlers run inline.
    using UiMarshaller = std::function<void(std::function<void()>)>;

    void SetUiMarshaller(UiMarshaller marshaller);

    // !\brief Append a handler. Order is the order they are asked in.
    void Register(IHotkeyHandler& handler);

    void Clear();

    // !\brief Offer a key press to the chain.
    // !\return True when a handler consumed it, so no macro should also run.
    [[nodiscard]] bool Dispatch(const std::string& key);

    // !\brief Who holds this key, or empty when nobody does.
    // Disabled handlers still count: their binding stays reserved.
    [[nodiscard]] std::string_view OwnerOfBinding(const std::string& key) const;

private:
    mutable std::mutex m_Mutex;
    std::vector<IHotkeyHandler*> m_Handlers;
    UiMarshaller m_Marshaller;
};
