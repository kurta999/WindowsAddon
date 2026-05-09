#include "pch.hpp"

bool XmlCanEntryLoader::Load(const std::filesystem::path& path, std::vector<std::unique_ptr<CanTxEntry>>& e)
{
    bool ret = true;
    boost::property_tree::ptree pt;
    try
    {
        read_xml(path.generic_string(), pt);
        for(const boost::property_tree::ptree::value_type& v : pt.get_child("CanUsbXml"))
        {
            std::string frame_id_str = v.second.get_child("ID").get_value<std::string>();
            uint32_t frame_id = 0;
            try
            {
                frame_id = std::stoi(frame_id_str, nullptr, 16);
            }
            catch(const std::exception& e)
            {
                LOG(LogLevel::Error, "Invalid FrameID format, stoi exception: {} (FrameID: {})", e.what(), frame_id_str);
                continue;
            }

            if(std::ranges::count(e, frame_id, &CanTxEntry::id) != 0)
            {
                LOG(LogLevel::Warning, "CAN frame with FrameID {} already in TX List, skipping", frame_id_str);
                continue;
            }

            char bytes[128] = { 0 };
            std::string hex_str = v.second.get_child("Data").get_value<std::string>();
            boost::algorithm::erase_all(hex_str, " ");
            if(hex_str.length() > 16)
                hex_str.erase(16, hex_str.length() - 16);
            utils::ConvertHexStringToBuffer(hex_str, std::span{ bytes });

            boost::optional<std::string> color, bg_color, is_font_face;
            boost::optional<bool>  is_bold;
            boost::optional<float> is_scale;
            utils::xml::ReadChildIfexists<std::string>(v, "Color",           color);
            utils::xml::ReadChildIfexists<std::string>(v, "BackgroundColor", bg_color);
            utils::xml::ReadChildIfexists<bool>       (v, "Bold",            is_bold);
            utils::xml::ReadChildIfexists<float>      (v, "Scale",           is_scale);
            utils::xml::ReadChildIfexists<std::string>(v, "FontFace",        is_font_face);

            std::optional<uint32_t> color_    = color    ? std::optional{utils::ColorStringToInt(*color)}    : std::nullopt;
            std::optional<uint32_t> bg_color_ = bg_color ? std::optional{utils::ColorStringToInt(*bg_color)} : std::nullopt;
            std::optional<bool>     is_bold_  = (is_bold && *is_bold) ? std::optional{true} : std::nullopt;
            std::optional<float>    is_scale_ = is_scale   ? std::optional{*is_scale}   : std::nullopt;
            std::optional<std::string> font_  = is_font_face ? std::optional{*is_font_face} : std::nullopt;

            size_t data_len = hex_str.length() / 2;
            e.push_back(std::make_unique<CanTxEntry>(frame_id, reinterpret_cast<uint8_t*>(bytes), static_cast<uint8_t>(data_len),
                v.second.get_child("Period").get_value<int>(),
                v.second.get_child("LogLevel").get_value<uint8_t>(),
                v.second.get_child("Favourite").get_value<uint8_t>(),
                v.second.get_child("Comment").get_value<std::string>(),
                color_, bg_color_, is_bold_, is_scale_, font_));
        }
    }
    catch(const boost::property_tree::xml_parser_error& e)
    {
        LOG(LogLevel::Error, "Exception thrown: {}, {}", e.filename(), e.what());
        ret = false;
    }
    catch(const std::exception& e)
    {
        LOG(LogLevel::Error, "Exception thrown: {}", e.what());
        ret = false;
    }
    return ret;
}

bool XmlCanEntryLoader::Save(const std::filesystem::path& path, std::vector<std::unique_ptr<CanTxEntry>>& e) const
{
    bool ret = true;
    boost::property_tree::ptree pt;
    auto& root_node = pt.add_child("CanUsbXml", boost::property_tree::ptree{});
    for(auto& i : e)
    {
        std::string hex;
        utils::ConvertHexBufferToString(i->data, hex);

        auto& frame_node = root_node.add_child("Frame", boost::property_tree::ptree{});
        frame_node.add("ID",        std::format("{:X}", i->id));
        frame_node.add("Data",      hex);
        frame_node.add("Period",    i->period);
        frame_node.add("LogLevel",  i->log_level);
        frame_node.add("Favourite", i->favourite_level);
        frame_node.add("Comment",   i->comment);

        if(i->m_color)             frame_node.add("Color",           utils::ColorIntToString(*i->m_color));
        if(i->m_bg_color)          frame_node.add("BackgroundColor", utils::ColorIntToString(*i->m_bg_color));
        if(i->m_is_bold)           frame_node.add("Bold", "1");
        if(i->m_scale != 1.0f)    frame_node.add("Scale",    std::format("{:.1f}", i->m_scale));
        if(!i->m_font_face.empty()) frame_node.add("FontFace", i->m_font_face);
    }

    try
    {
        boost::property_tree::write_xml(path.generic_string(), pt, std::locale(),
            boost::property_tree::xml_writer_make_settings<boost::property_tree::ptree::key_type>('\t', 1));
    }
    catch(const std::exception& e)
    {
        LOG(LogLevel::Error, "Exception thrown: {}", e.what());
        ret = false;
    }
    return ret;
}

bool XmlCanRxEntryLoader::Load(const std::filesystem::path& path, std::unordered_map<uint32_t, std::string>& e, std::unordered_map<uint32_t, uint8_t>& loglevels)
{
    bool ret = true;
    boost::property_tree::ptree pt;
    try
    {
        read_xml(path.generic_string(), pt);
        for(const boost::property_tree::ptree::value_type& v : pt.get_child("CanUsbRxXml"))
        {
            std::string frame_id_str = v.second.get_child("ID").get_value<std::string>();
            uint32_t frame_id = 0;
            try
            {
                frame_id = std::stoi(frame_id_str, nullptr, 16);
            }
            catch(const std::exception& e)
            {
                LOG(LogLevel::Error, "Invalid FrameID format, stoi exception: {} (FrameID: {})", e.what(), frame_id_str);
                continue;
            }

            if(std::ranges::count(e, frame_id, [](const auto& item) { return item.first; }) != 0)
            {
                LOG(LogLevel::Warning, "CAN frame with FrameID {} already in RX List, skipping", frame_id_str);
                continue;
            }

            e[frame_id]         = v.second.get_child("Comment").get_value<std::string>();
            loglevels[frame_id] = v.second.get_child("LogLevel").get_value<uint8_t>();
        }
    }
    catch(const boost::property_tree::xml_parser_error& e)
    {
        LOG(LogLevel::Error, "Exception thrown: {}, {}", e.filename(), e.what());
        ret = false;
    }
    catch(const std::exception& e)
    {
        LOG(LogLevel::Error, "Exception thrown: {}", e.what());
        ret = false;
    }
    return ret;
}

bool XmlCanRxEntryLoader::Save(const std::filesystem::path& path, std::unordered_map<uint32_t, std::string>& e, std::unordered_map<uint32_t, uint8_t>& loglevels) const
{
    bool ret = true;
    boost::property_tree::ptree pt;
    auto& root_node = pt.add_child("CanUsbRxXml", boost::property_tree::ptree{});
    for(auto& [id, comment] : e)
    {
        auto& frame_node = root_node.add_child("Frame", boost::property_tree::ptree{});
        frame_node.add("ID",       std::format("{:X}", id));
        frame_node.add("Comment",  comment);
        frame_node.add("LogLevel", loglevels[id]);
    }

    try
    {
        boost::property_tree::write_xml(path.generic_string(), pt, std::locale(),
            boost::property_tree::xml_writer_make_settings<boost::property_tree::ptree::key_type>('\t', 1));
    }
    catch(const std::exception& e)
    {
        LOG(LogLevel::Error, "Exception thrown: {}", e.what());
        ret = false;
    }
    return ret;
}

bool XmlCanMappingLoader::Load(const std::filesystem::path& path, CanMapping& mapping, CanFrameMetadataMap& metadata)
{
    bool ret = true;
    boost::property_tree::ptree pt;
    try
    {
        read_xml(path.generic_string(), pt);
        for(const boost::property_tree::ptree::value_type& v : pt.get_child("CanFrameMapping"))
        {
            std::string frame_id_str = v.second.get_child("ID").get_value<std::string>();
            std::string frame_name   = v.second.get_child("Name").get_value<std::string>();
            uint32_t frame_id = 0;
            try
            {
                frame_id = std::stoi(frame_id_str, nullptr, 16);
            }
            catch(const std::exception& e)
            {
                LOG(LogLevel::Error, "Invalid FrameID format, stoi exception: {} (FrameID: {})", e.what(), frame_id_str);
                continue;
            }

            if(frame_name.empty())
                LOG(LogLevel::Warning, "Empty frame name for FrameID: {:X}", frame_id);

            auto& meta      = metadata[frame_id];
            meta.name       = std::move(frame_name);
            meta.size       = v.second.get_child("Size").get_value<uint8_t>();
            meta.direction  = v.second.get_child("Direction").get_value<char>();

            uint8_t calculated_size = 0;
            for(const boost::property_tree::ptree::value_type& m : v.second)
            {
                if(m.first != "Mapping")
                    continue;

                uint8_t     offset = m.second.get<uint8_t>("<xmlattr>.offset");
                uint8_t     len    = m.second.get<uint8_t>("<xmlattr>.len");
                std::string type   = m.second.get<std::string>("<xmlattr>.type");
                std::string name   = m.second.get_value<std::string>();

                CanBitfieldType bitfield_type = GetTypeFromString(type);
                if(bitfield_type == CBT_INVALID)
                {
                    LOG(LogLevel::Warning, "Invalid type for frame mapping. FrameID: {:X}, type: {}", frame_id, type);
                    continue;
                }

                if(mapping.contains(frame_id) &&
                   std::ranges::count(mapping[frame_id], offset, [](const auto& item) { return item.first; }) != 0)
                {
                    LOG(LogLevel::Warning, "Duplicate offset for FrameID: {}, Name: {}, Offset: {} - skipping", frame_id_str, name, offset);
                    continue;
                }

                auto [type_min, type_max] = GetMinMaxForType(bitfield_type);

                int64_t min_val = type_min;
                int64_t max_val = type_max;
                uint32_t color    = wxBLACK->GetRGB();
                uint32_t bg_color = DEFAULT_TXTCTRL_BACKGROUND;
                bool     is_bold  = false;
                float    scale    = 1.0f;

                if(auto v_ = m.second.get_optional<int64_t>("<xmlattr>.min"))
                    min_val = std::max(*v_, type_min);
                if(auto v_ = m.second.get_optional<int64_t>("<xmlattr>.max"))
                    max_val = std::min(*v_, type_max);
                if(auto v_ = m.second.get_optional<std::string>("<xmlattr>.color"))
                    color = utils::ColorStringToInt(*v_);
                if(auto v_ = m.second.get_optional<std::string>("<xmlattr>.bg_color"))
                    bg_color = utils::ColorStringToInt(*v_);
                if(auto v_ = m.second.get_optional<bool>("<xmlattr>.bold"))
                    is_bold = *v_;
                if(auto v_ = m.second.get_optional<float>("<xmlattr>.scale"))
                    scale = *v_;

                std::string description;
                if(auto v_ = m.second.get_optional<std::string>("<xmlattr>.desc"))
                {
                    description = *v_;
                    boost::algorithm::replace_all(description, "\\n", "\n");
                }

                mapping[frame_id].try_emplace(offset, std::make_unique<CanMap>(
                    std::move(name), bitfield_type, len, min_val, max_val,
                    std::move(description), color, bg_color, is_bold, scale));
                calculated_size += len;
            }

            if((calculated_size / 8) > meta.size)
                LOG(LogLevel::Warning, "Calculated size for frame {} ({}) is bigger than predefined {}!", meta.name, calculated_size, meta.size);
        }
    }
    catch(const boost::property_tree::xml_parser_error& e)
    {
        LOG(LogLevel::Error, "Exception thrown: {}, {}", e.filename(), e.what());
        ret = false;
    }
    catch(const std::exception& e)
    {
        LOG(LogLevel::Error, "Exception thrown: {}", e.what());
        ret = false;
    }
    return ret;
}

bool XmlCanMappingLoader::Save(const std::filesystem::path& path, CanMapping& mapping, const CanFrameMetadataMap& metadata) const
{
    bool ret = true;
    boost::property_tree::ptree pt;
    auto& root_node = pt.add_child("CanFrameMapping", boost::property_tree::ptree{});
    for(auto& [frame_id, fields] : mapping)
    {
        auto& frame_node = root_node.add_child("Frame", boost::property_tree::ptree{});
        frame_node.add("ID", std::format("{:X}", frame_id));

        const CanFrameMetadata* meta = nullptr;
        if(auto it = metadata.find(frame_id); it != metadata.end())
            meta = &it->second;

        frame_node.add("Name",      meta ? meta->name      : "");
        frame_node.add("Size",      meta ? meta->size      : uint8_t{0});
        frame_node.add("Direction", std::format("{:c}", meta ? meta->direction : 'T'));

        for(auto& [offset, m] : fields)
        {
            auto& child = frame_node.add("Mapping", m->m_Name);
            child.put("<xmlattr>.offset", offset);
            child.put("<xmlattr>.len",    m->m_Size);
            child.put("<xmlattr>.type",   GetStringFromType(m->m_Type));
            child.put("<xmlattr>.min",    m->m_MinVal);
            child.put("<xmlattr>.max",    m->m_MaxVal);

            if(m->m_color != wxBLACK->GetRGB())
                child.put("<xmlattr>.color",    utils::ColorIntToString(m->m_color));
            if(m->m_bg_color != DEFAULT_TXTCTRL_BACKGROUND)
                child.put("<xmlattr>.bg_color", utils::ColorIntToString(m->m_bg_color));
            if(m->m_is_bold)
                child.put("<xmlattr>.bold", true);
            if(m->m_scale != 1.0f)
                child.put("<xmlattr>.scale", m->m_scale);

            if(!m->m_Description.empty())
            {
                std::string desc = m->m_Description;
                boost::algorithm::replace_all(desc, "\n", "\\n");
                child.put("<xmlattr>.desc", desc);
            }
        }
    }

    try
    {
        boost::property_tree::write_xml(path.generic_string(), pt, std::locale(),
            boost::property_tree::xml_writer_make_settings<boost::property_tree::ptree::key_type>('\t', 1));
    }
    catch(const std::exception& e)
    {
        LOG(LogLevel::Error, "Exception thrown: {}", e.what());
        ret = false;
    }
    return ret;
}

CanBitfieldType XmlCanMappingLoader::GetTypeFromString(std::string_view input)
{
    auto ret = std::ranges::find_if(m_CanBitfieldTypeMap, [input](const auto& item) { return item.second == input; });
    return ret != m_CanBitfieldTypeMap.cend() ? ret->first : CBT_INVALID;
}

std::string_view XmlCanMappingLoader::GetStringFromType(CanBitfieldType type)
{
    auto it = m_CanBitfieldTypeMap.find(type);
    return it != m_CanBitfieldTypeMap.end() ? it->second : m_CanBitfieldTypeMap[CBT_INVALID];
}

std::pair<int64_t, int64_t> XmlCanMappingLoader::GetMinMaxForType(CanBitfieldType type)
{
    auto it = m_CanTypeSizes.find(type);
    return it != m_CanTypeSizes.end() ? it->second : m_CanTypeSizes[CBT_INVALID];
}
