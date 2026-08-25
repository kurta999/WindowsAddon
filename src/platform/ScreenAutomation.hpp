#pragma once

#include "interface/IScreenAutomation.hpp"

namespace platform
{
// !\brief IScreenAutomation on top of the ImageRecognition free functions.
//
// The adapter exists so the macro engine can be given a screen without
// depending on OpenCV or the desktop, which is what the layering needs.
class ScreenAutomation final : public IScreenAutomation
{
public:
    bool BringWindowToForeground(const std::string& process_name, const std::string& window_name) override;
    bool FindImageOnScreen(const std::string& image_name, int& x, int& y) override;
    void MoveCursorAndClick(int x, int y) override;
};
}
