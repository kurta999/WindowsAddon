#pragma once

#include <string>

// The alarm handler has to ask the user how long an alarm should run for. In
// the application that is a wxWidgets dialog owned by AlarmPanel, which is also
// the only reason the handler ever needed the main frame.
class IAlarmPrompt
{
public:
    virtual ~IAlarmPrompt() = default;

    // !\brief Asks the user for a duration, blocking until they answer.
    // !\param pump_timer_once [in] Drive the dialog's own timer once before
    // waiting. The tray icon path needs this because it arms an alarm from
    // outside the GUI timer.
    // !\return The duration as the user typed it, for example "1h30m".
    // An empty string means "no duration", which parses to zero.
    [[nodiscard]] virtual std::string AskForDuration(bool pump_timer_once) = 0;
};
