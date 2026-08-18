#pragma once

#include <cstdint>
#include <compare>
#include <string>

// Presentation values are domain data, not GUI widgets. Adapters translate
// these values to wx types at the edge of the application.
struct TextStyle
{
    TextStyle() = default;
    TextStyle(uint32_t color, uint32_t bg_color, bool is_bold, float scale, const std::string& font_face = "") :
        m_color(color), m_bg_color(bg_color), m_is_bold(is_bold), m_scale(scale), m_font_face(font_face)
    {

    }

    // !\brief Text color
    uint32_t m_color{};

    // !\brief Text background color
    uint32_t m_bg_color{};

    // !\brief Is text bold?
    bool m_is_bold{ false };

    // !\brief Text scale
    float m_scale{ 1.0f };

    // !\brief Font face
    std::string m_font_face;
};

struct LogicalSize
{
    int width = -1;
    int height = -1;

    [[nodiscard]] bool IsDefault() const noexcept { return width < 0 || height < 0; }
    auto operator<=>(const LogicalSize&) const = default;
};
