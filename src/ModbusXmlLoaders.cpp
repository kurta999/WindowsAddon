#include "pch_core.hpp"
#include "ModbusHandler.hpp"
#include "utils/XmlDocument.hpp"
#include "utils/TextStyleXml.hpp"
#include "ModbusJsonPersistence.hpp"
#include "Logger.hpp"
#include "Utils.hpp"

/* Getting a device layout on and off disk.

   XmlModbusEntryLoader reads and writes the XML form; HybridModbusEntryLoader
   sniffs the file and hands off to either that or the JSON persistence. Neither
   knows anything about polling, which is the rest of what ModbusHandler.cpp
   used to contain. */

std::optional<ModbusDeviceLayout> XmlModbusEntryLoader::Load(const std::filesystem::path& path, uint32_t branch,
    uint8_t fallback_slave_id)
{
    ModbusDeviceLayout layout;
    layout.slaveId = fallback_slave_id;

    /* Named references onto the layout being built. These were six separate
       out-parameters; the parsing below is unchanged, and a failure now
       discards a local rather than leaving the caller's tables half-filled. */
    uint8_t& slave_id = layout.slaveId;
    ModbusItemType& coils = layout.coils;
    ModbusItemType& input_status = layout.inputStatus;
    ModbusItemType& holding = layout.holding;
    ModbusItemType& input = layout.input;
    NumModbusEntries& num_entries = layout.counts;

    size_t register_offset_coils = 0;
    size_t register_offset_input_status = 0;
    size_t register_offset_holding = 0;
    size_t register_offset_input = 0;

    auto document = utils::xml::Load(path);
    if(!document)
        return std::nullopt;

    boost::property_tree::ptree& pt = *document;
    try
    {
        for(const boost::property_tree::ptree::value_type& v : pt.get_child("Modbus"))
        {
            if(v.first == "NumCoils")           { num_entries.coils           = v.second.get_value<size_t>(0); continue; }
            if(v.first == "NumInputStatus")     { num_entries.inputStatus     = v.second.get_value<size_t>(0); continue; }
            if(v.first == "NumHoldingRegisters"){ num_entries.holdingRegisters = v.second.get_value<size_t>(0); continue; }
            if(v.first == "NumInput" || v.first == "NumInputRegisters")
                                                  { num_entries.inputRegisters  = v.second.get_value<size_t>(0); continue; }
            if(v.first == "CoilsOffset")        { num_entries.coilsOffset     = v.second.get_value<uint16_t>(0); continue; }
            if(v.first == "InputStatusOffset")  { num_entries.inputStatusOffset = v.second.get_value<uint16_t>(0); continue; }
            if(v.first == "InputOffset")        { num_entries.inputOffset     = v.second.get_value<uint16_t>(0); continue; }
            if(v.first == "HoldingOffset")      { num_entries.holdingOffset   = v.second.get_value<uint16_t>(0); continue; }
            if(v.first == "SlaveAddress")       { slave_id = v.second.get_value<uint8_t>(0); continue; }

            ModbusItemType* item = nullptr;
            size_t* offset = nullptr;
            ModbusBitfieldType register_value_type = ModbusBitfieldType::MBT_BOOL;

            for(const boost::property_tree::ptree::value_type& m : v.second)
            {
                if(m.first == "Coil")
                {
                    item = &coils;  offset = &register_offset_coils;
                    register_value_type = ModbusBitfieldType::MBT_BOOL;
                }
                else if(m.first == "InputStatus")
                {
                    item = &input_status;  offset = &register_offset_input_status;
                    register_value_type = ModbusBitfieldType::MBT_BOOL;
                }
                else if(m.first == "Input")
                {
                    item = &input;  offset = &register_offset_input;
                    register_value_type = ModbusBitfieldType::MBT_UI16;
                }
                else if(m.first == "Holding")
                {
                    item = &holding;  offset = &register_offset_holding;
                    register_value_type = ModbusBitfieldType::MBT_UI16;
                }
                else
                {
                    LOG(LogLevel::Warning, "Invalid modbus register child in Modbus.xml: {}", m.first);
                    continue;
                }

                std::string name = m.second.get_child("Name").get_value<std::string>();
                boost::optional<uint8_t> fav_child_val = m.second.get_optional<uint8_t>("FavLevel");
                uint8_t fav_level = fav_child_val ? *fav_child_val : 0;

                uint64_t last_val = m.second.get<uint64_t>("LastVal", 0);
                const size_t configured_offset = m.second.get<size_t>("Offset", *offset);

                boost::optional<std::string> data_type = m.second.get_optional<std::string>("DataType");
                if (data_type)
                    register_value_type = GetTypeFromString(*data_type);

                ModbusValueFormat val_format = ModbusValueFormat::MVF_DEC;
                boost::optional<std::string> val_format_str = m.second.get_optional<std::string>("Format");
                if (val_format_str)
                {
                    if      (*val_format_str == "hex") val_format = ModbusValueFormat::MVF_HEX;
                    else if (*val_format_str == "bin") val_format = ModbusValueFormat::MVF_BIN;
                }

                boost::optional<int64_t> min_val_child = m.second.get_optional<int64_t>("Min");
                boost::optional<int64_t> max_val_child = m.second.get_optional<int64_t>("Max");

                std::string description;
                boost::optional<std::string> description_child = m.second.get_optional<std::string>("Desc");
                if (description_child)
                {
                    description = *description_child;
                    boost::algorithm::replace_all(description, "\\n", "\n");
                }

                boost::optional<std::string> branch_str;
                utils::xml::ReadChildIfexists<std::string>(m, "Branch", branch_str);

                const utils::xml::OptionalTextStyle style = utils::xml::ReadOptionalTextStyle(m);
                const std::optional<uint32_t>& color_ = style.color;
                const std::optional<uint32_t>& bg_color_ = style.bg_color;
                const std::optional<bool>& is_bold_ = style.is_bold;
                const std::optional<float>& is_scale_ = style.scale;
                const std::optional<std::string>& is_font_face_ = style.font_face;

                ModbusMapping mapping;
                for (const boost::property_tree::ptree::value_type& x : m.second)
                {
                    if (x.first != "Mapping") continue;

                    uint8_t map_offset = x.second.get<uint8_t>("<xmlattr>.offset");
                    uint8_t len        = x.second.get<uint8_t>("<xmlattr>.len");
                    std::string type   = x.second.get<std::string>("<xmlattr>.type");
                    std::string map_name = x.second.get_value<std::string>();

                    ModbusBitfieldType bitfield_type = GetTypeFromString(type);
                    if (bitfield_type == MBT_INVALID)
                    {
                        LOG(LogLevel::Warning, "Invalid type used for frame mapping. Type: {}", type);
                        continue;
                    }

                    uint32_t color_val    = DEFAULT_TXTCTRL_FOREGROUND;
                    uint32_t bg_color_val = DEFAULT_TXTCTRL_BACKGROUND;
                    bool is_bold_val      = false;
                    float scale_val       = 1.0f;

                    boost::optional<std::string> color_child    = x.second.get_optional<std::string>("<xmlattr>.color");
                    boost::optional<std::string> bg_color_child = x.second.get_optional<std::string>("<xmlattr>.bg_color");
                    boost::optional<bool>  is_bold_child        = x.second.get_optional<bool>("<xmlattr>.bold");
                    boost::optional<float> scale_child          = x.second.get_optional<float>("<xmlattr>.scale");

                    if (color_child)    color_val    = utils::ColorStringToInt(*color_child);
                    if (bg_color_child) bg_color_val = utils::ColorStringToInt(*bg_color_child);
                    if (is_bold_child)  is_bold_val  = *is_bold_child;
                    if (scale_child)    scale_val    = *scale_child;

                    std::string map_desc;
                    boost::optional<std::string> desc_child = x.second.get_optional<std::string>("<xmlattr>.desc");
                    if (desc_child)
                    {
                        map_desc = *desc_child;
                        boost::algorithm::replace_all(map_desc, "\\n", "\n");
                    }

                    auto ptr_map = std::make_unique<ModbusMap>(std::move(map_name), bitfield_type, len,
                        std::numeric_limits<int64_t>::min(), std::numeric_limits<int64_t>::max(),
                        std::move(map_desc), color_val, bg_color_val, is_bold_val, scale_val);

                    mapping.try_emplace(map_offset, std::move(ptr_map));
                }

                auto ptr = std::make_unique<ModbusItem>(name, fav_level, configured_offset, register_value_type, val_format, description, mapping, 0, 0, last_val,
                    color_, bg_color_, is_bold_, is_scale_, is_font_face_);

                if (branch_str)
                {
                    std::vector<std::string> branch_list;
                    boost::split(branch_list, *branch_str, [](char c) { return c == ','; }, boost::algorithm::token_compress_on);
                    for (auto& b : branch_list)
                        ptr->branches |= ModbusEntryHandler::getBranchIDByName(b);
                }
                else
                {
                    ptr->branches = branch ? branch : 0xFFFFFFFFu;
                }

                *offset = std::max(*offset, configured_offset + ptr->GetSize());
                item->push_back(std::move(ptr));
            }
        }

        if (num_entries.coils           == 0xFFFF) num_entries.coils           = register_offset_coils;
        if (num_entries.inputStatus     == 0xFFFF) num_entries.inputStatus     = register_offset_input_status;
        if (num_entries.holdingRegisters == 0xFFFF) num_entries.holdingRegisters = register_offset_holding;
        if (num_entries.inputRegisters  == 0xFFFF) num_entries.inputRegisters  = register_offset_input;
    }
    catch(const std::exception& e)
    {
        /* utils::xml::Load already reported anything that was not well-formed,
           so what reaches here is a document that parsed but does not contain
           what this loader expects - a missing element or an unreadable value. */
        LOG(LogLevel::Error, "Malformed {}: {}", path.generic_string(), e.what());
        return std::nullopt;
    }
    return layout;
}

bool HybridModbusEntryLoader::IsJson(const std::filesystem::path& path)
{
    std::string extension = path.extension().generic_string();
    std::ranges::transform(extension, extension.begin(), [](unsigned char c)
        { return static_cast<char>(std::tolower(c)); });
    return extension == ".json";
}

std::optional<ModbusDeviceLayout> HybridModbusEntryLoader::Load(const std::filesystem::path& path,
    uint32_t branch, uint8_t fallback_slave_id)
{
    if(!IsJson(path))
        return m_xml.Load(path, branch, fallback_slave_id);

    try
    {
        return modbus_json::LoadLayout(path, branch, fallback_slave_id, m_selectedDevice, &m_selectedDevice);
    }
    catch(const std::exception& e)
    {
        LOG(LogLevel::Error, "Failed to load Modbus JSON {}: {}", path.generic_string(), e.what());
        return std::nullopt;
    }
}

bool HybridModbusEntryLoader::Save(const std::filesystem::path& path,
    const ModbusDeviceLayout& layout) const
{
    if(!IsJson(path))
        return m_xml.Save(path, layout);
    try
    {
        return modbus_json::SaveLayout(path, layout, m_selectedDevice, true);
    }
    catch(const std::exception& e)
    {
        LOG(LogLevel::Error, "Failed to save Modbus JSON {}: {}", path.generic_string(), e.what());
        return false;
    }
}

std::vector<std::string> HybridModbusEntryLoader::GetAvailableDevices(const std::filesystem::path& path) const
{
    if(!IsJson(path))
        return {};
    try
    {
        return modbus_json::LoadDeviceNames(path);
    }
    catch(const std::exception& e)
    {
        LOG(LogLevel::Error, "Failed to inspect Modbus JSON {}: {}", path.generic_string(), e.what());
        return {};
    }
}

bool HybridModbusEntryLoader::SelectDevice(const std::string& device)
{
    m_selectedDevice = device;
    return true;
}

bool XmlModbusEntryLoader::Save(const std::filesystem::path& path, const ModbusDeviceLayout& layout) const
{
    /* Named references onto the layout, mirroring Load above; the writing below
       is unchanged. Saving never mutated these, but it took them by non-const
       reference, so nothing said so. */
    const uint8_t& slave_id = layout.slaveId;
    const NumModbusEntries& num_entries = layout.counts;

    boost::property_tree::ptree pt;
    auto& root_node = pt.add_child("Modbus", boost::property_tree::ptree{});
    root_node.add("SlaveAddress", slave_id);
    root_node.add("NumCoils", num_entries.coils);
    root_node.add("NumInputStatus", num_entries.inputStatus);
    root_node.add("NumHoldingRegisters", num_entries.holdingRegisters);
    root_node.add("NumInputRegisters", num_entries.inputRegisters);
    root_node.add("CoilsOffset", num_entries.coilsOffset);
    root_node.add("InputStatusOffset", num_entries.inputStatusOffset);
    root_node.add("HoldingOffset", num_entries.holdingOffset);
    root_node.add("InputOffset", num_entries.inputOffset);

    static const std::array<std::string, 4> child_names    = { "Coils", "InputStatuses", "HoldingRegisters", "InputRegisters" };
    static const std::array<std::string, 4> subchild_names = { "Coil", "InputStatus", "Holding", "Input" };
    static const std::array<std::string, 3> format_strs    = { "dec", "hex", "bin" };

    const ModbusItemType* items[] = { &layout.coils, &layout.inputStatus, &layout.holding, &layout.input };
    for(int id = 0; id < 4; id++)
    {
        auto& register_node = root_node.add_child(child_names[id], boost::property_tree::ptree{});
        for(const auto& m : *items[id])
        {
            auto& sub_node = register_node.add_child(subchild_names[id], boost::property_tree::ptree{});
            sub_node.add("Name", m->m_Name);
            sub_node.add("Offset", m->m_Offset);
            sub_node.add("FavLevel", m->m_FavLevel);
            sub_node.add("DataType", GetStringFromType(m->m_Type));
            sub_node.add("Format", format_strs[static_cast<size_t>(m->m_Format)]);
            sub_node.add("Min", m->m_Min);
            sub_node.add("Max", m->m_Max);
            if (!m->m_Desc.empty())
            {
                std::string desc_str = m->m_Desc;
                boost::algorithm::replace_all(desc_str, "\n", "\\n");
                sub_node.add("Desc", desc_str);
            }
            sub_node.add("LastVal", m->m_Value.Integer());
            utils::xml::WriteOptionalTextStyle(sub_node, m->m_color, m->m_bg_color,
                m->m_is_bold, m->m_scale, m->m_font_face);
        }
    }

    return utils::xml::Save(path, pt);
}
