#pragma once

#include <string>
#include <string_view>

// !\brief One link in the global hotkey chain.
//
// Feature keys used to be an if-chain inside CustomMacro that named six
// services directly, and the same six were listed a second time in
// IsKeyReserved. Adding a feature meant editing both lists, and the two
// silently disagreeing was a live bug rather than a hypothetical one. A handler
// declares the key it holds once; the registry answers both questions from it.
class IHotkeyHandler
{
public:
    virtual ~IHotkeyHandler() = default;

    // !\brief The key this handler currently holds, or empty when none is bound.
    [[nodiscard]] virtual std::string HotkeyBinding() const = 0;

    // !\brief Name used in the "key is already assigned to X" warning.
    [[nodiscard]] virtual std::string_view HotkeyOwner() const = 0;

    // !\brief Run the action. Only called once the binding has matched.
    virtual void OnHotkeyPressed() = 0;

    // !\brief Whether the handler wants its key right now.
    // A disabled feature keeps its binding reserved but does not act on it,
    // which is how AntiLock has always behaved.
    [[nodiscard]] virtual bool IsHotkeyEnabled() const { return true; }

    // !\brief Whether the action has to run on the UI thread.
    // Hotkeys arrive on the serial reader thread, so anything that touches a
    // window is marshalled by the registry instead of by each caller.
    [[nodiscard]] virtual bool HotkeyNeedsUiThread() const { return false; }
};
