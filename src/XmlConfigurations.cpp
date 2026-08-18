#include "XmlConfigurations.hpp"

#include <boost/property_tree/ptree.hpp>
#include <boost/property_tree/xml_parser.hpp>

#include <algorithm>
#include <charconv>
#include <limits>
#include <set>
#include <string_view>

namespace xml_config
{
namespace
{
using boost::property_tree::ptree;

bool Write(const std::filesystem::path& path, const ptree& tree)
{
    try
    {
        boost::property_tree::write_xml(path.string(), tree, std::locale(),
            boost::property_tree::xml_writer_make_settings<std::string>(' ', 2));
        return true;
    }
    catch(...)
    {
        return false;
    }
}

template<typename Callback>
bool ReadChildren(const std::filesystem::path& path, const char* root, Callback&& callback)
{
    try
    {
        ptree tree;
        boost::property_tree::read_xml(path.string(), tree);
        for(const auto& child : tree.get_child(root))
            if(!callback(child)) return false;
        return true;
    }
    catch(...)
    {
        return false;
    }
}

std::optional<std::uint32_t> ParseHexId(std::string_view text, std::uint32_t maximum)
{
    std::uint32_t id = 0;
    const auto result = std::from_chars(text.data(), text.data() + text.size(), id, 16);
    if(result.ec != std::errc{} || result.ptr != text.data() + text.size() || id > maximum || text.empty())
        return std::nullopt;
    return id;
}

std::string FormatHex(std::uint32_t value)
{
    char output[9]{};
    const auto result = std::to_chars(output, output + sizeof(output), value, 16);
    std::string text(output, result.ptr);
    std::ranges::transform(text, text.begin(), [](unsigned char ch) { return static_cast<char>(std::toupper(ch)); });
    return text;
}

std::optional<std::vector<std::uint8_t>> ParseBytes(std::string text)
{
    text.erase(std::remove_if(text.begin(), text.end(), [](unsigned char ch) { return std::isspace(ch) != 0; }), text.end());
    if(text.size() > 16 || text.size() % 2 != 0)
        return std::nullopt;
    std::vector<std::uint8_t> bytes;
    for(std::size_t index = 0; index < text.size(); index += 2)
    {
        auto value = ParseHexId(std::string_view(text).substr(index, 2), 0xFF);
        if(!value) return std::nullopt;
        bytes.push_back(static_cast<std::uint8_t>(*value));
    }
    return bytes;
}

std::string FormatBytes(const std::vector<std::uint8_t>& bytes)
{
    constexpr char hex[] = "0123456789ABCDEF";
    std::string result;
    for(const auto byte : bytes)
    {
        result.push_back(hex[byte >> 4]);
        result.push_back(hex[byte & 0x0F]);
    }
    return result;
}

bool IsDidType(std::string_view type)
{
    constexpr std::string_view types[] = {"uint8_t", "uint16_t", "uint32_t", "uint64_t", "string", "bytearray"};
    return std::ranges::find(types, type) != std::end(types);
}

std::optional<unsigned> TypeWidth(std::string_view type)
{
    if(type == "bool") return 1;
    if(type == "uint8_t" || type == "int8_t") return 8;
    if(type == "uint16_t" || type == "int16_t") return 16;
    if(type == "uint32_t" || type == "int32_t" || type == "float") return 32;
    if(type == "uint64_t" || type == "int64_t" || type == "double") return 64;
    return std::nullopt;
}
}

bool SaveTx(const std::filesystem::path& path, const std::vector<TxEntry>& entries)
{
    ptree tree, root;
    for(const auto& entry : entries)
    {
        ptree frame;
        frame.put("ID", FormatHex(entry.id));
        frame.put("Data", FormatBytes(entry.data));
        frame.put("Period", entry.period);
        frame.put("LogLevel", entry.log_level);
        frame.put("Favourite", entry.favourite);
        frame.put("Comment", entry.comment);
        root.add_child("Frame", frame);
    }
    tree.add_child("CanUsbXml", root);
    return Write(path, tree);
}

std::optional<std::vector<TxEntry>> LoadTx(const std::filesystem::path& path)
{
    std::vector<TxEntry> entries;
    std::set<std::uint32_t> ids;
    const bool ok = ReadChildren(path, "CanUsbXml", [&](const auto& child) {
        if(child.first != "Frame") return true;
        const auto id_text = child.second.template get_optional<std::string>("ID");
        const auto data_text = child.second.template get_optional<std::string>("Data");
        const auto period = child.second.template get_optional<std::uint32_t>("Period");
        const auto log = child.second.template get_optional<unsigned>("LogLevel");
        const auto favourite = child.second.template get_optional<unsigned>("Favourite");
        const auto comment = child.second.template get_optional<std::string>("Comment");
        if(!id_text || !data_text || !period || !log || !favourite || !comment || *log > 255 || *favourite > 255)
            return false;
        auto id = ParseHexId(*id_text, 0x1FFFFFFF);
        auto data = ParseBytes(*data_text);
        if(!id || !data || !ids.insert(*id).second) return false;
        entries.push_back({*id, std::move(*data), *period, static_cast<std::uint8_t>(*log),
                           static_cast<std::uint8_t>(*favourite), *comment});
        return true;
    });
    return ok ? std::optional{std::move(entries)} : std::nullopt;
}

bool SaveRx(const std::filesystem::path& path, const std::vector<RxEntry>& entries)
{
    ptree tree, root;
    for(const auto& entry : entries)
    {
        ptree frame;
        frame.put("ID", FormatHex(entry.id));
        frame.put("Comment", entry.comment);
        frame.put("LogLevel", entry.log_level);
        root.add_child("Frame", frame);
    }
    tree.add_child("CanUsbRxXml", root);
    return Write(path, tree);
}

std::optional<std::vector<RxEntry>> LoadRx(const std::filesystem::path& path)
{
    std::vector<RxEntry> entries;
    std::set<std::uint32_t> ids;
    const bool ok = ReadChildren(path, "CanUsbRxXml", [&](const auto& child) {
        if(child.first != "Frame") return true;
        const auto id_text = child.second.template get_optional<std::string>("ID");
        const auto log = child.second.template get_optional<unsigned>("LogLevel");
        const auto comment = child.second.template get_optional<std::string>("Comment");
        if(!id_text || !log || !comment || *log > 255) return false;
        auto id = ParseHexId(*id_text, 0x1FFFFFFF);
        if(!id || !ids.insert(*id).second) return false;
        entries.push_back({*id, static_cast<std::uint8_t>(*log), *comment});
        return true;
    });
    return ok ? std::optional{std::move(entries)} : std::nullopt;
}

bool SaveDids(const std::filesystem::path& path, const std::vector<DidEntry>& entries)
{
    ptree tree, root;
    for(const auto& entry : entries)
    {
        ptree did;
        did.put("ID", FormatHex(entry.id));
        did.put("Type", entry.type);
        did.put("Name", entry.name);
        did.put("Min", entry.min);
        did.put("Max", entry.max);
        did.put("Length", entry.length);
        root.add_child("DidEntry", did);
    }
    tree.add_child("DidListXml", root);
    return Write(path, tree);
}

std::optional<std::vector<DidEntry>> LoadDids(const std::filesystem::path& path)
{
    std::vector<DidEntry> entries;
    std::set<std::uint32_t> ids;
    const bool ok = ReadChildren(path, "DidListXml", [&](const auto& child) {
        if(child.first != "DidEntry") return true;
        const auto id_text = child.second.template get_optional<std::string>("ID");
        const auto type = child.second.template get_optional<std::string>("Type");
        const auto name = child.second.template get_optional<std::string>("Name");
        const auto min = child.second.template get_optional<std::string>("Min");
        const auto max = child.second.template get_optional<std::string>("Max");
        const auto length = child.second.template get_optional<std::size_t>("Length");
        if(!id_text || !type || !name || !min || !max || !length || !IsDidType(*type) || *length > 4096)
            return false;
        auto id = ParseHexId(*id_text, 0xFFFF);
        if(!id || !ids.insert(*id).second) return false;
        entries.push_back({static_cast<std::uint16_t>(*id), *type, *name, *min, *max, *length});
        return true;
    });
    return ok ? std::optional{std::move(entries)} : std::nullopt;
}

bool SaveAlarms(const std::filesystem::path& path, const std::vector<AlarmEntry>& entries)
{
    ptree tree, root;
    for(const auto& entry : entries)
    {
        ptree alarm;
        alarm.put("Name", entry.name);
        alarm.put("Trigger", entry.trigger);
        alarm.put("TriggerKey", entry.trigger_key);
        alarm.put("Execute", entry.execute);
        alarm.put("ShowDialog", entry.show_dialog);
        root.add_child("Alarm", alarm);
    }
    tree.add_child("AlarmsXml", root);
    return Write(path, tree);
}

std::optional<std::vector<AlarmEntry>> LoadAlarms(const std::filesystem::path& path)
{
    std::vector<AlarmEntry> entries;
    const bool ok = ReadChildren(path, "AlarmsXml", [&](const auto& child) {
        if(child.first != "Alarm") return true;
        const auto name = child.second.template get_optional<std::string>("Name");
        const auto trigger = child.second.template get_optional<std::string>("Trigger");
        const auto key = child.second.template get_optional<std::string>("TriggerKey");
        const auto execute = child.second.template get_optional<std::string>("Execute");
        const auto show = child.second.template get_optional<bool>("ShowDialog");
        if(!name || !trigger || !key || !execute || !show || (*trigger != "Macro" && *trigger != "Gui"))
            return false;
        entries.push_back({*name, *trigger, *key, *execute, *show});
        return true;
    });
    return ok ? std::optional{std::move(entries)} : std::nullopt;
}

bool SaveMappings(const std::filesystem::path& path, const std::vector<FrameMapping>& entries)
{
    ptree tree, root;
    for(const auto& entry : entries)
    {
        ptree frame;
        frame.put("ID", FormatHex(entry.id));
        frame.put("Name", entry.name);
        frame.put("Size", entry.size);
        frame.put("Direction", entry.direction);
        for(const auto& field : entry.fields)
        {
            ptree mapping(field.name);
            mapping.put("<xmlattr>.offset", field.offset);
            mapping.put("<xmlattr>.len", field.length);
            mapping.put("<xmlattr>.type", field.type);
            mapping.put("<xmlattr>.desc", field.description);
            frame.add_child("Mapping", mapping);
        }
        root.add_child("Frame", frame);
    }
    tree.add_child("CanFrameMapping", root);
    return Write(path, tree);
}

std::optional<std::vector<FrameMapping>> LoadMappings(const std::filesystem::path& path)
{
    std::vector<FrameMapping> entries;
    std::set<std::uint32_t> ids;
    const bool ok = ReadChildren(path, "CanFrameMapping", [&](const auto& child) {
        if(child.first != "Frame") return true;
        const auto id_text = child.second.template get_optional<std::string>("ID");
        const auto name = child.second.template get_optional<std::string>("Name");
        const auto size = child.second.template get_optional<unsigned>("Size");
        const auto direction = child.second.template get_optional<char>("Direction");
        if(!id_text || !name || !size || !direction || *size > 8 || (*direction != 'T' && *direction != 'R'))
            return false;
        auto id = ParseHexId(*id_text, 0x1FFFFFFF);
        if(!id || !ids.insert(*id).second) return false;

        FrameMapping frame{*id, *name, static_cast<std::uint8_t>(*size), *direction, {}};
        std::set<unsigned> offsets;
        for(const auto& mapping : child.second)
        {
            if(mapping.first != "Mapping") continue;
            const auto offset = mapping.second.template get_optional<unsigned>("<xmlattr>.offset");
            const auto length = mapping.second.template get_optional<unsigned>("<xmlattr>.len");
            const auto type = mapping.second.template get_optional<std::string>("<xmlattr>.type");
            const auto description = mapping.second.template get_optional<std::string>("<xmlattr>.desc");
            const auto width = type ? TypeWidth(*type) : std::nullopt;
            if(!offset || !length || !type || !description || !width || *length == 0 || *length > *width ||
               *offset + *length > *size * 8 || !offsets.insert(*offset).second)
                return false;
            frame.fields.push_back({static_cast<std::uint8_t>(*offset), static_cast<std::uint8_t>(*length),
                                    *type, mapping.second.template get_value<std::string>(), *description});
        }
        entries.push_back(std::move(frame));
        return true;
    });
    return ok ? std::optional{std::move(entries)} : std::nullopt;
}
}
