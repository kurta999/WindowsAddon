#include "ModbusJsonPersistence.hpp"

/* No pch_core.hpp: this compiles into the external and Modbus test targets
   too, which do not use the precompiled header and would otherwise pick up
   <Windows.h> and its min/max macros through it. The header above already
   names everything these bodies use. */

/* The bodies that used to be `inline` in the header. Nothing here changed
   except the keyword and the qualification. */

std::string modbus_json::DeviceNameFromBranchName(const std::string& branch_name)
{
    const std::string normalized = NormalizeName(branch_name);
    if(normalized == "pglnew" || normalized == "pgl")
        return "PGL-NEW";
    if(normalized == "cdmre")
        return "CDMRE-Connect";
    if(normalized == "cdmrn")
        return "CDMRN-Connect";
    return {};
}

std::string modbus_json::DeviceDisplayName(const std::string& json_device_name)
{
    const std::string normalized = NormalizeName(json_device_name);
    if(normalized == "pglnew" || normalized == "pgl")
        return "PGL";
    if(normalized == "cdmreconnect" || normalized == "cdmre")
        return "CDMRE";
    if(normalized == "cdmrnconnect" || normalized == "cdmrn")
        return "CDMRN";
    return json_device_name;
}

std::vector<std::string> modbus_json::LoadJsonDeviceNames(const std::filesystem::path& path)
{
    boost::property_tree::ptree tree;
    boost::property_tree::read_json(path.generic_string(), tree);
    std::vector<std::string> devices;
    for(const auto& node : tree)
        if(!IsSettingsNode(node.first) && IsDeviceNode(node.second))
            devices.push_back(node.first);
    return devices;
}

std::vector<std::string> modbus_json::LoadDeviceNames(const std::filesystem::path& path)
{
    auto devices = LoadJsonDeviceNames(path);
    std::ranges::transform(devices, devices.begin(), [](const std::string& device)
        { return DeviceDisplayName(device); });
    return devices;
}

std::string modbus_json::ResolveDeviceName(const std::vector<std::string>& devices, const std::string& requested)
{
    const std::string normalized = NormalizeName(requested);
    const std::string mapped = DeviceNameFromBranchName(requested);
    const auto match = std::ranges::find_if(devices, [&](const std::string& device)
        {
            return NormalizeName(device) == normalized ||
                NormalizeName(DeviceDisplayName(device)) == normalized ||
                (!mapped.empty() && NormalizeName(device) == NormalizeName(mapped));
        });
    if(match != devices.end())
        return *match;
    return devices.empty() ? std::string{} : devices.front();
}

ModbusValueScaling modbus_json::GetScaling(const boost::property_tree::ptree& node)
{
    ModbusValueScaling scaling;
    const auto child = node.get_child_optional("scaling");
    if(!child)
        return scaling;
    scaling.enabled = child->get<bool>("enabled", false);
    scaling.x1 = child->get<double>("x1", scaling.x1);
    scaling.y1 = child->get<double>("y1", scaling.y1);
    scaling.x2 = child->get<double>("x2", scaling.x2);
    scaling.y2 = child->get<double>("y2", scaling.y2);
    scaling.precision = static_cast<uint8_t>(std::clamp(child->get<int>("precision", 2), 0, 9));
    return scaling;
}

size_t modbus_json::LoadRegisterArray(const boost::property_tree::ptree& device, const char* array_name,
    ModbusItemType& output, ModbusBitfieldType default_type, uint32_t branch, bool force_type)
{
    const auto array = device.get_child_optional(array_name);
    if(!array)
        return 0;
    size_t register_count = 0;
    for(const auto& entry : *array)
    {
        const auto& node = entry.second;
        if(node.get<bool>("is_unused", false))
            continue;
        const ModbusBitfieldType type = force_type ? default_type :
            StringToModbusBitfieldType(node.get<std::string>("type", ModbusBitfieldTypeToString(default_type)));
        if(type == MBT_INVALID)
            continue;
        const size_t offset = node.get<size_t>("offset");
        const size_t size = force_type ? 1 : node.get<size_t>("size", ModbusItem::GetTypeSize(type));
        const std::string format = node.get<std::string>("format", "dec");
        const ModbusValueFormat value_format = format == "hex" ? MVF_HEX : format == "bin" ? MVF_BIN : MVF_DEC;
        std::string description = GetEither<std::string>(node, "desc", "description", "");
        boost::algorithm::replace_all(description, "\\n", "\n");
        const auto bold_value = node.get_optional<bool>("bold");
        const auto scale_value = node.get_optional<float>("scale");
        const auto font_value = node.get_optional<std::string>("font_face");
        const std::optional<bool> bold = bold_value ? std::optional<bool>(*bold_value) : std::nullopt;
        const std::optional<float> scale = scale_value ? std::optional<float>(*scale_value) : std::nullopt;
        const std::optional<std::string> font = font_value ? std::optional<std::string>(*font_value) : std::nullopt;
        auto item = std::make_unique<ModbusItem>(node.get<std::string>("name"),
            static_cast<uint8_t>(GetEither<unsigned>(node, "fav_level", "FavLevel", 0)),
            offset, type, value_format, description, node.get<int64_t>("min", 0),
            node.get<int64_t>("max", 0), GetEither<uint64_t>(node, "last_val", "LastVal", 0),
            GetColor(node, "color"), GetColor(node, "background_color"), bold, scale, font,
            StringToRegisterByteOrder(node.get<std::string>("byte_order", "default")));
        item->m_RegisterSize = size;
        item->m_FloatPrecision = static_cast<uint8_t>(std::clamp(node.get<int>("float_precision", 3), 0, 9));
        item->m_ManualAddress = node.get<int>("manual_address", -1);
        item->m_ValueScaling = GetScaling(node);
        if(!IsModbusScalingSupported(type) || value_format != MVF_DEC)
            item->m_ValueScaling.enabled = false;
        if(const auto colors = node.get_child_optional("conditional_colors"))
        {
            size_t index = 0;
            for(const auto& color : *colors)
            {
                if(index == item->m_ConditionalColors.size())
                    break;
                auto& rule = item->m_ConditionalColors[index++];
                rule.comparison = StringToComparison(color.second.get<std::string>("comparison", "not_used"));
                rule.value = color.second.get<double>("value", 0.0);
                rule.color = GetColor(color.second, "color");
                rule.background_color = GetColor(color.second, "background_color");
            }
        }
        item->branches = branch ? branch : 0xFFFFFFFFu;
        output.push_back(std::move(item));
        register_count = std::max(register_count, offset + size);
    }
    return register_count;
}

bool modbus_json::LoadDeviceFile(const std::filesystem::path& path, uint8_t& slave_id,
    ModbusItemType& coils, ModbusItemType& input_status, ModbusItemType& holding,
    ModbusItemType& input, NumModbusEntries& counts, uint32_t branch, const std::string& requested_device,
    std::string* loaded_device)
{
    boost::property_tree::ptree tree;
    boost::property_tree::read_json(path.generic_string(), tree);
    const std::string device_name = ResolveDeviceName(LoadJsonDeviceNames(path), requested_device);
    if(device_name.empty())
        throw std::runtime_error("The Modbus JSON file does not contain a device register table");

    coils.clear(); input_status.clear(); holding.clear(); input.clear(); counts = {};
    if(const auto settings = tree.get_child_optional("Settings"))
        slave_id = static_cast<uint8_t>(settings->get<unsigned>("default_slave_id", slave_id));
    const auto& device = tree.get_child(device_name);
    const bool cdmr = NormalizeName(device_name).starts_with("cdmr");
    counts.coilsOffset = static_cast<uint16_t>(device.get<unsigned>("coils_offset", 0));
    counts.inputStatusOffset = static_cast<uint16_t>(device.get<unsigned>("input_status_offset", cdmr ? 10000 : 0));
    counts.inputOffset = static_cast<uint16_t>(device.get<unsigned>("input_register_offset", cdmr ? 30000 : 0));
    counts.holdingOffset = static_cast<uint16_t>(device.get<unsigned>("holding_register_offset", cdmr ? 40000 : 0));
    counts.coils = LoadRegisterArray(device, "coils", coils, MBT_BOOL, branch, true);
    counts.inputStatus = LoadRegisterArray(device, "input status", input_status, MBT_BOOL, branch, true);
    counts.inputRegisters = LoadRegisterArray(device, "input registers", input, MBT_UI16, branch);
    counts.holdingRegisters = LoadRegisterArray(device, "holding registers", holding, MBT_UI16, branch);
    if(loaded_device)
        *loaded_device = device_name;
    return true;
}

void modbus_json::AppendRegisterArray(boost::property_tree::ptree& device, const char* name,
    const ModbusItemType& items)
{
    boost::property_tree::ptree array;
    for(const auto& item : items)
    {
        boost::property_tree::ptree node;
        node.put("name", item->m_Name); node.put("offset", item->m_Offset);
        node.put("size", item->GetSize()); node.put("type", ModbusBitfieldTypeToString(item->m_Type));
        node.put("byte_order", RegisterByteOrderToString(item->m_NetworkByteOrder));
        if(item->m_Format != MVF_DEC) node.put("format", item->m_Format == MVF_HEX ? "hex" : "bin");
        if(item->m_FavLevel) node.put("fav_level", static_cast<unsigned>(item->m_FavLevel));
        if(!item->m_Desc.empty()) { auto desc = item->m_Desc; boost::algorithm::replace_all(desc, "\n", "\\n"); node.put("desc", desc); }
        if(item->m_Value.Integer()) node.put("last_val", item->m_Value.Integer());
        if(item->m_color) node.put("color", utils::FormatColor(*item->m_color));
        if(item->m_bg_color) node.put("background_color", utils::FormatColor(*item->m_bg_color));
        if(item->m_is_bold) node.put("bold", true);
        if(item->m_scale != 1.0f) node.put("scale", item->m_scale);
        if(!item->m_font_face.empty()) node.put("font_face", item->m_font_face);
        if((item->m_Type == MBT_FLOAT || item->m_Type == MBT_DOUBLE) && item->m_FloatPrecision != 3)
            node.put("float_precision", static_cast<unsigned>(item->m_FloatPrecision));
        if(item->m_ManualAddress >= 0) node.put("manual_address", item->m_ManualAddress);
        boost::property_tree::ptree colors;
        for(const auto& rule : item->m_ConditionalColors)
        {
            if(rule.comparison == ModbusConditionalColorComparison::NotUsed && !rule.color && !rule.background_color)
                continue;
            boost::property_tree::ptree color;
            color.put("comparison", ComparisonToString(rule.comparison)); color.put("value", rule.value);
            if(rule.color) color.put("color", utils::FormatColor(*rule.color));
            if(rule.background_color) color.put("background_color", utils::FormatColor(*rule.background_color));
            colors.push_back({ "", color });
        }
        if(!colors.empty()) node.add_child("conditional_colors", colors);
        if(ShouldSaveScaling(item->m_ValueScaling))
        {
            boost::property_tree::ptree scaling;
            scaling.put("enabled", IsModbusScalingActive(*item));
            scaling.put("x1", item->m_ValueScaling.x1); scaling.put("y1", item->m_ValueScaling.y1);
            scaling.put("x2", item->m_ValueScaling.x2); scaling.put("y2", item->m_ValueScaling.y2);
            scaling.put("precision", static_cast<unsigned>(item->m_ValueScaling.precision));
            node.add_child("scaling", scaling);
        }
        array.push_back({ "", node });
    }
    device.put_child(name, array);
}

std::filesystem::path modbus_json::NextBackupPath(const std::filesystem::path& path)
{
    for(size_t index = 1; index < 10000; ++index)
    {
        auto backup = path.parent_path() /
            (path.stem().generic_string() + " (" + std::to_string(index) + ")" + path.extension().generic_string());
        if(!std::filesystem::exists(backup))
            return backup;
    }
    throw std::runtime_error("Could not find an available JSON backup filename");
}

bool modbus_json::SaveDeviceFile(const std::filesystem::path& path, uint8_t slave_id,
    const ModbusItemType& coils, const ModbusItemType& input_status, const ModbusItemType& holding,
    const ModbusItemType& input, const NumModbusEntries& counts, const std::string& device_name,
    bool backup_existing)
{
    boost::property_tree::ptree tree;
    if(std::filesystem::exists(path))
        boost::property_tree::read_json(path.generic_string(), tree);
    boost::property_tree::ptree settings;
    if(const auto existing_settings = tree.get_child_optional("Settings"))
        settings = *existing_settings;
    settings.put("default_slave_id", static_cast<unsigned>(slave_id));
    tree.put_child("Settings", settings);
    boost::property_tree::ptree device;
    device.put("coils_offset", counts.coilsOffset);
    device.put("input_status_offset", counts.inputStatusOffset);
    device.put("input_register_offset", counts.inputOffset);
    device.put("holding_register_offset", counts.holdingOffset);
    AppendRegisterArray(device, "coils", coils);
    AppendRegisterArray(device, "input status", input_status);
    AppendRegisterArray(device, "input registers", input);
    AppendRegisterArray(device, "holding registers", holding);
    tree.put_child(device_name.empty() ? "Device" : device_name, device);
    if(backup_existing && std::filesystem::exists(path))
        std::filesystem::copy_file(path, NextBackupPath(path));
    boost::property_tree::write_json(path.generic_string(), tree, std::locale(), true);
    return true;
}

bool modbus_json::SaveDeviceFile(const std::filesystem::path& path, uint8_t slave_id,
    const ModbusItemType& coils, const ModbusItemType& input_status, const ModbusItemType& holding,
    const ModbusItemType& input, const std::string& device_name, bool backup_existing)
{
    NumModbusEntries counts;
    counts.coilsOffset = 0;
    counts.inputStatusOffset = 0;
    counts.inputOffset = 0;
    counts.holdingOffset = 0;
    return SaveDeviceFile(path, slave_id, coils, input_status, holding, input, counts,
        device_name, backup_existing);
}

std::optional<ModbusDeviceLayout> modbus_json::LoadLayout(const std::filesystem::path& path,
    uint32_t branch, uint8_t fallback_slave_id, const std::string& requested_device,
    std::string* loaded_device)
{
    ModbusDeviceLayout layout;
    layout.slaveId = fallback_slave_id;
    if(!LoadDeviceFile(path, layout.slaveId, layout.coils, layout.inputStatus, layout.holding,
        layout.input, layout.counts, branch, requested_device, loaded_device))
    {
        return std::nullopt;
    }
    return layout;
}

bool modbus_json::SaveLayout(const std::filesystem::path& path, const ModbusDeviceLayout& layout,
    const std::string& device_name, bool backup_existing)
{
    return SaveDeviceFile(path, layout.slaveId, layout.coils, layout.inputStatus, layout.holding,
        layout.input, layout.counts, device_name, backup_existing);
}
