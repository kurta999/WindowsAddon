#include "pch_core.hpp"

#include "platform/ScreenAutomation.hpp"

#include "ImageRecognition.hpp"

namespace platform
{
bool ScreenAutomation::BringWindowToForeground(const std::string& process_name, const std::string& window_name)
{
    return ImageRecognition::BringWindowToForegroundByName(process_name, window_name);
}

bool ScreenAutomation::FindImageOnScreen(const std::string& image_name, int& x, int& y)
{
    return ImageRecognition::FindImageOnScreen(image_name, x, y);
}

void ScreenAutomation::MoveCursorAndClick(int x, int y)
{
    POINT pos{};
    pos.x = x;
    pos.y = y;
    ImageRecognition::MoveCursorAndClick(pos);
}
}
