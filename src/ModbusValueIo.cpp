#include "ModbusValueIo.hpp"
#include "utils/NumberParsing.hpp"

#include <format>
#include <sstream>

namespace modbus_values
{
namespace
{
// !\brief The section headings, in the order Format writes them.
constexpr std::string_view kCoilsHeading = "Coils";
constexpr std::string_view kInputStatusHeading = "Input Status";
constexpr std::string_view kHoldingHeading = "Holding Registers";
constexpr std::string_view kInputHeading = "Input Registers";

enum class Section
{
    None,
    Coils,
    InputStatus,
    HoldingRegisters,
    InputRegisters
};

/* The held alternative formats itself. The ternary this replaced made both arms
   float, so a 64-bit integer register was exported through a float and lost its
   low digits. */
[[nodiscard]] std::string ValueText(const ModbusItem& item)
{
    return item.m_Value.Visit([](auto value) { return std::format("{}", value); });
}

void AppendBoolTable(std::string& out, std::string_view heading, const ModbusItemType& items)
{
    out += heading;
    out += '\n';
    for(const auto& item : items)
        out += std::format("{}: {}\n", item->m_Name, item->m_Value.Integer() != 0);
}

void AppendValueTable(std::string& out, std::string_view heading, const ModbusItemType& items)
{
    out += heading;
    out += '\n';
    for(const auto& item : items)
        out += std::format("{}: {}\n", item->m_Name, ValueText(*item));
}
}

void Clear(ModbusDeviceLayout& layout)
{
    for(auto* items : { &layout.coils, &layout.inputStatus, &layout.holding, &layout.input })
        for(auto& item : *items)
            item->m_Value.Reset();
}

std::string Format(const ModbusDeviceLayout& layout)
{
    std::string out;
    AppendBoolTable(out, kCoilsHeading, layout.coils);
    AppendBoolTable(out, kInputStatusHeading, layout.inputStatus);
    AppendValueTable(out, kHoldingHeading, layout.holding);
    AppendValueTable(out, kInputHeading, layout.input);
    return out;
}

std::vector<ModbusWrite> Apply(ModbusDeviceLayout& layout, std::string_view text)
{
    std::vector<ModbusWrite> writes;
    std::istringstream stream{ std::string(text) };
    std::string line;
    Section section = Section::None;

    while(std::getline(stream, line))
    {
        /* Exported files are written with '\n', but one that has been through a
           Windows editor arrives with '\r\n' and the heading match then fails
           silently, importing nothing. */
        if(!line.empty() && line.back() == '\r')
            line.pop_back();

        if(line == kCoilsHeading)        { section = Section::Coils;            continue; }
        if(line == kInputStatusHeading)  { section = Section::InputStatus;      continue; }
        if(line == kHoldingHeading)      { section = Section::HoldingRegisters; continue; }
        if(line == kInputHeading)        { section = Section::InputRegisters;   continue; }

        std::istringstream fields(line);
        std::string name;
        std::string value_text;
        if(!std::getline(fields, name, ':') || !(fields >> value_text))
            continue;

        name = std::string(utils::detail::TrimView(name));

        const bool is_float = value_text.find('.') != std::string::npos;
        const bool is_bool = value_text.find("true") != std::string::npos ||
            value_text.find("false") != std::string::npos;

        /* This is a file the user picked, so a malformed line must not throw
           out of the import; stoull/stof did. */
        uint64_t value = (is_float || is_bool) ? 0 : utils::ParseOr<uint64_t>(value_text, 0);
        const float float_value = (is_float && !is_bool) ? utils::ParseOr<float>(value_text, 0.0f) : 0.0f;
        if(is_bool)
            value = value_text.find("true") != std::string::npos;

        if(section == Section::Coils)
        {
            std::size_t id = 0;
            for(auto& item : layout.coils)
            {
                if(item->m_Name != name)
                {
                    id++;
                    continue;
                }

                if(item->m_Value.SetInteger(value))
                    writes.push_back(ModbusCoilWrite{ id, value != 0 });
                break;
            }
        }
        else if(section == Section::HoldingRegisters)
        {
            std::size_t id = 0;
            for(auto& item : layout.holding)
            {
                if(item->m_Name != name)
                {
                    id++;
                    continue;
                }

                /* A float register needs the float write queue. Routing it
                   through a holding write truncated the imported value to an
                   integer before it ever reached the wire - and would also
                   replace the stored float with an integer. */
                if(item->m_Type == ModbusBitfieldType::MBT_FLOAT)
                {
                    if(item->m_Value.SetFloat(float_value))
                        writes.push_back(ModbusFloatWrite{ id, float_value });
                }
                else if(item->m_Value.SetInteger(value))
                {
                    writes.push_back(ModbusHoldingWrite{ id, value });
                }
                break;
            }
        }
    }

    return writes;
}
}
