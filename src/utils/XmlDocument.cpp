#include "pch_core.hpp"

#include "utils/XmlDocument.hpp"

#include "Logger.hpp"

#include <boost/property_tree/ptree.hpp>
#include <boost/property_tree/xml_parser.hpp>

namespace utils::xml
{
std::optional<Tree> Load(const std::filesystem::path& path)
{
    Tree tree;
    try
    {
        boost::property_tree::read_xml(path.generic_string(), tree);
    }
    catch(const boost::property_tree::xml_parser_error& e)
    {
        /* xml_parser_error carries the file and line; std::exception::what()
           alone does not, and "Exception thrown" with no file name was the
           least useful log line in the project. */
        LOG(LogLevel::Error, "Could not read XML file {}: {} (line {})",
            e.filename(), e.message(), e.line());
        return std::nullopt;
    }
    catch(const std::exception& e)
    {
        LOG(LogLevel::Error, "Could not read XML file {}: {}", path.generic_string(), e.what());
        return std::nullopt;
    }
    return tree;
}

bool Save(const std::filesystem::path& path, const Tree& tree)
{
    try
    {
        boost::property_tree::write_xml(path.generic_string(), tree, std::locale(),
            boost::property_tree::xml_writer_make_settings<Tree::key_type>('\t', 1));
    }
    catch(const std::exception& e)
    {
        LOG(LogLevel::Error, "Could not write XML file {}: {}", path.generic_string(), e.what());
        return false;
    }
    return true;
}

bool LoadEntries(const std::filesystem::path& path, std::string_view root,
    const std::function<void(const Tree::value_type&)>& per_entry)
{
    std::optional<Tree> document = Load(path);
    if(!document)
        return false;

    try
    {
        for(const Tree::value_type& entry : document->get_child(std::string(root)))
            per_entry(entry);
    }
    catch(const std::exception& e)
    {
        /* Load already reported anything that was not well-formed, so what
           reaches here is a document that parsed but does not contain what the
           loader expects - a missing element or an unreadable value. */
        LOG(LogLevel::Error, "Malformed {}: {}", path.generic_string(), e.what());
        return false;
    }
    return true;
}

bool SaveEntries(const std::filesystem::path& path, std::string_view root,
    const std::function<void(Tree&)>& fill)
{
    Tree document;
    Tree& root_node = document.add_child(std::string(root), Tree{});
    fill(root_node);
    return Save(path, document);
}

void ReportInvalidHex(std::string_view element, std::string_view raw, int digits)
{
    /* The accepted range comes from the type the caller asked for, so a 16-bit
       DID reports 0-FFFF and a 32-bit frame id reports 0-FFFFFFFF. */
    const std::string largest(static_cast<size_t>(digits), 'F');
    LOG(LogLevel::Error, "Invalid {} format, expected hexadecimal 0-{} ({}: {})",
        element, largest, element, raw);
}
}
