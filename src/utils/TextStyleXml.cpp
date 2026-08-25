#include "pch_core.hpp"

#include "utils/TextStyleXml.hpp"
#include "utils/XmlDocument.hpp"

#include "Utils.hpp"

#include <format>

#include <boost/property_tree/ptree.hpp>

namespace utils::xml
{
OptionalTextStyle ReadOptionalTextStyle(const boost::property_tree::ptree::value_type& node)
{
    boost::optional<std::string> color;
    boost::optional<std::string> bg_color;
    boost::optional<bool> is_bold;
    boost::optional<float> scale;
    boost::optional<std::string> font_face;

    ReadChildIfexists<std::string>(node, text_style_element::kColor, color);
    ReadChildIfexists<std::string>(node, text_style_element::kBackgroundColor, bg_color);
    ReadChildIfexists<bool>(node, text_style_element::kBold, is_bold);
    ReadChildIfexists<float>(node, text_style_element::kScale, scale);
    ReadChildIfexists<std::string>(node, text_style_element::kFontFace, font_face);

    OptionalTextStyle style;
    if(color)               style.color = utils::ColorStringToInt(*color);
    if(bg_color)            style.bg_color = utils::ColorStringToInt(*bg_color);
    if(is_bold && *is_bold) style.is_bold = true;
    if(scale)               style.scale = *scale;
    if(font_face)           style.font_face = *font_face;
    return style;
}

void WriteOptionalTextStyle(boost::property_tree::ptree& node,
    const std::optional<uint32_t>& color,
    const std::optional<uint32_t>& bg_color,
    bool is_bold,
    float scale,
    const std::string& font_face)
{
    if(color)
        node.add(text_style_element::kColor, utils::ColorIntToString(*color));
    if(bg_color)
        node.add(text_style_element::kBackgroundColor, utils::ColorIntToString(*bg_color));
    if(is_bold)
        node.add(text_style_element::kBold, "1");
    if(scale != 1.0f)
        node.add(text_style_element::kScale, std::format("{:.1f}", scale));
    if(!font_face.empty())
        node.add(text_style_element::kFontFace, font_face);
}
}
