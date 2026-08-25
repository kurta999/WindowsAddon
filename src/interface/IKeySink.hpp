#pragma once

#include <string_view>

// !\brief Where a keypad device delivers what the user pressed.
//
// The serial keypad and the Corsair key block each run their own receive
// thread, and each pushed straight into CustomMacro::Get(). Those were the two
// edges pointing from a device driver up into the macro engine, and they are
// what made the drivers untestable without booting it.
//
// The two halves are separate because the devices deliver different things: the
// serial keypad sends bytes that still have to be parsed into a frame, and the
// HID driver has already decoded a key by the time it calls.
class IKeySink
{
public:
    virtual ~IKeySink() = default;

    // !\brief Bytes from the serial keypad, still to be parsed.
    virtual void OnKeypadData(std::string_view data) = 0;

    // !\brief A key its own driver has already named, the way the macro
    // bindings spell it - the HID driver decodes the report itself.
    virtual void OnKeyPressed(std::string_view key) = 0;
};
