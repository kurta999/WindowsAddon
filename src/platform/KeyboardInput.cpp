#include "platform/KeyboardInput.hpp"

#ifdef _WIN32
/* WIN32_LEAN_AND_MEAN comes from the build, project-wide. */
#include <Windows.h>
#endif

namespace platform
{
void TapNumlock()
{
#ifdef _WIN32
    INPUT input = { 0 };
    input.type = INPUT_KEYBOARD;
    input.ki.wVk = VK_NUMLOCK;
    input.ki.dwFlags = 0;
    SendInput(1, &input, sizeof(input));
    input.ki.dwFlags = KEYEVENTF_KEYUP;
    SendInput(1, &input, sizeof(input));
#endif
}
}
