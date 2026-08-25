#pragma once

#include <chrono>
#include <string>

class AlarmEntry;

// Everything the alarm handler has to tell the rest of the application about.
// Keeping it behind an interface is what lets the handler run without a GUI:
// the notifications go to the main frame, and firing a macro goes to
// CustomMacro, neither of which the handler should know about.
class IAlarmEventSink
{
public:
    virtual ~IAlarmEventSink() = default;

    // !\brief An alarm definition was read from the configuration. The
    // application uses this to register its trigger key with the macro engine.
    virtual void OnAlarmLoaded(const AlarmEntry& entry) = 0;

    // !\brief An alarm was armed and is now counting down.
    virtual void OnAlarmArmed(const std::string& name, std::chrono::seconds duration) = 0;

    // !\brief An armed alarm reached zero.
    virtual void OnAlarmTriggered(const std::string& name) = 0;

    // !\brief A macro-triggered alarm expired and its key has to be pressed.
    virtual void OnAlarmMacroRequested(const std::string& trigger_key) = 0;
};
