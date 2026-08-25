#pragma once

#include <cstdint>
#include <optional>
#include <string>

#include <boost/property_tree/ptree_fwd.hpp>

// !\brief The presentation half of a configuration entry, as it appears in XML.
//
// The five element names below were string literals in three loaders, thirty
// occurrences in all, so renaming one meant finding the other twenty-nine.
//
// Only the "optional" flavour is shared here, because only it is genuinely
// shared: CanXmlLoaders and ModbusHandler both treat an absent element as "no
// override" and write an element only when the value differs from the default.
// CmdExecutor writes all five unconditionally onto a plain TextStyle, which is
// a different policy and stays where it is - collapsing the two behind one
// parameterised helper would cost more than the duplication it removed.
namespace utils::xml
{
namespace text_style_element
{
inline constexpr const char* kColor = "Color";
inline constexpr const char* kBackgroundColor = "BackgroundColor";
inline constexpr const char* kBold = "Bold";
inline constexpr const char* kScale = "Scale";
inline constexpr const char* kFontFace = "FontFace";
}

// !\brief Presentation overrides read from a configuration file. A field that
// the file did not set stays empty, so the loaded entry keeps its default.
struct OptionalTextStyle
{
    std::optional<uint32_t> color;
    std::optional<uint32_t> bg_color;
    std::optional<bool> is_bold;
    std::optional<float> scale;
    std::optional<std::string> font_face;
};

// !\brief Read the five presentation elements from one entry's node.
//
// `is_bold` is set only when the file says true, matching what both callers did
// by hand: a `<Bold>0</Bold>` is the same as no element at all.
[[nodiscard]] OptionalTextStyle ReadOptionalTextStyle(const boost::property_tree::ptree::value_type& node);

// !\brief Write the presentation elements that differ from the defaults.
void WriteOptionalTextStyle(boost::property_tree::ptree& node,
    const std::optional<uint32_t>& color,
    const std::optional<uint32_t>& bg_color,
    bool is_bold,
    float scale,
    const std::string& font_face);
}
