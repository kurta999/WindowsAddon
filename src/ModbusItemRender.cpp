#include "ModbusItemRender.hpp"

#include "ModbusConditionalColors.hpp"
#include "ModbusTypeTraits.hpp"

#include <bitset>
#include <format>

namespace
{
std::string FormatInteger(const ModbusItem& item)
{
    const uint64_t raw = item.m_Value.Integer();
    switch(item.m_Format)
    {
        case ModbusValueFormat::MVF_HEX:
            return std::format("{:X}", raw);

        case ModbusValueFormat::MVF_BIN:
        {
            const std::string binary = std::bitset<64>(raw).to_string();
            const auto first = binary.find('1');
            return first == std::string::npos ? std::string{ "0" } : binary.substr(first);
        }

        case ModbusValueFormat::MVF_DEC:
        default:
            /* A signed type has to be narrowed to its own width before it is
               widened again, or a negative value prints as a huge positive one. */
            switch(item.m_Type)
            {
                case MBT_I16: return std::format("{}", static_cast<int16_t>(raw));
                case MBT_I32: return std::format("{}", static_cast<int32_t>(raw));
                case MBT_I64: return std::format("{}", static_cast<int64_t>(raw));
                default:      return std::format("{}", raw);
            }
    }
}

std::string FormatValue(const ModbusItem& item)
{
    if(IsModbusScalingActive(item))
        return std::format("{:.{}f}", GetModbusItemDisplayNumericValue(item),
            static_cast<int>(item.m_ValueScaling.precision));

    if(item.m_Type == ModbusBitfieldType::MBT_FLOAT)
        return std::format("{:.{}f}", item.m_Value.Float(), static_cast<int>(item.m_FloatPrecision));

    if(item.m_Type == ModbusBitfieldType::MBT_DOUBLE)
        return std::format("{:.{}f}", item.m_Value.Double(), static_cast<int>(item.m_FloatPrecision));

    return FormatInteger(item);
}
}

ModbusCellRender RenderModbusItem(const ModbusItem& item)
{
    ModbusCellRender render;
    render.text = FormatValue(item);

    /* A matching conditional rule overrides each colour independently: a rule
       that only sets a background leaves the item's own text colour alone. */
    const ModbusConditionalColorRule* rule = FindMatchingModbusConditionalColorRule(item);
    render.color = rule && rule->color ? rule->color : item.m_color;
    render.background_color = rule && rule->background_color ? rule->background_color : item.m_bg_color;

    return render;
}
