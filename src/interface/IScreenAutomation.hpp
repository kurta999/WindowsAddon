#pragma once

#include <string>

#ifndef _WINDEF_
struct tagPOINT;
#endif

// !\brief What a macro action needs from the screen: find a picture on it, move
// the pointer, raise a window.
//
// KeyFindImageOnScreen and KeyBringWindowToForeground called ImageRecognition's
// free functions directly, which made the macro engine - otherwise a small,
// self-contained interpreter - depend on OpenCV and the desktop. That single
// dependency is what stopped src/ being split into layered libraries: it was the
// only edge pointing from the core up towards a feature.
//
// The macro action receives this through MacroContext, the same way it already
// receives its command executor.
class IScreenAutomation
{
public:
    virtual ~IScreenAutomation() = default;

    // !\brief Raise the window named `window_name` belonging to `process_name`.
    virtual bool BringWindowToForeground(const std::string& process_name,
        const std::string& window_name) = 0;

    // !\brief Locate `image_name` on screen. On success `x` and `y` are its centre.
    virtual bool FindImageOnScreen(const std::string& image_name, int& x, int& y) = 0;

    // !\brief Move the pointer to (x, y) and click.
    virtual void MoveCursorAndClick(int x, int y) = 0;
};
