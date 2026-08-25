#pragma once

#include "utils/NumberParsing.hpp"

#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <string_view>

#include <boost/property_tree/ptree.hpp>

// !\brief Reading and writing this project's XML configuration files.
//
// Six loaders used to open the same way - construct a ptree, call read_xml,
// catch std::exception, log "Exception thrown: {}", set a bool - and write the
// same way, each repeating the writer settings that decide what the files look
// like on disk. That was sixteen copies of the same eight lines, and the one
// that mattered most (the indent character and depth) had to agree in eight
// places for the files to stay diff-able.
//
// This is the file half only. What the tree means is each loader's own job.
namespace utils::xml
{
using Tree = boost::property_tree::ptree;

// !\brief Parse an XML file.
// !\return The document, or nothing when the file cannot be read or is not
//          well-formed. Both cases are logged; the caller decides what a
//          missing configuration file means for it.
[[nodiscard]] std::optional<Tree> Load(const std::filesystem::path& path);

// !\brief Write an XML file in this project's format: tab-indented, one level
//         per depth, so the configuration files stay readable in a diff.
// !\return false, having logged, when the file could not be written.
[[nodiscard]] bool Save(const std::filesystem::path& path, const Tree& tree);

// !\brief Walk the entries under `root`, calling `per_entry` for each.
//
// Every loader in this tree opened and closed the same way: Load, bail on
// nothing, iterate get_child(root) inside a try, and catch std::exception to
// log "Malformed {}: {}". Eight copies, right down to a three-line comment
// pasted into all eight catch blocks. What differs between loaders is only
// what they do with an entry.
//
// `per_entry` may throw - a missing element or an unreadable value does - and
// that ends the load the same way it always did, reported once, here.
// !\return false when the file could not be read, or an entry threw.
[[nodiscard]] bool LoadEntries(const std::filesystem::path& path, std::string_view root,
    const std::function<void(const Tree::value_type&)>& per_entry);

// !\brief Build a document with a single `root` element and write it.
// !\param fill Adds the entries; receives the root element, not the document.
[[nodiscard]] bool SaveEntries(const std::filesystem::path& path, std::string_view root,
    const std::function<void(Tree&)>& fill);

// !\brief Log that a hexadecimal element could not be read. Not for direct use;
// ReadHexChild calls it. Out of line so this header does not pull in Logger.
void ReportInvalidHex(std::string_view element, std::string_view raw, int digits);

// !\brief Read a child element written as hexadecimal.
//
// Five loaders spelled this out as get_child -> TryParse(base 16) -> log and
// skip, three of them carrying the same explanatory comment about std::stoi
// accepting "12G" as 0x12.
// !\return Nothing, having logged, when the element is not valid hexadecimal.
template <typename T>
[[nodiscard]] std::optional<T> ReadHexChild(const Tree::value_type& v, const std::string& element)
{
    const std::string raw = v.second.get_child(element).get_value<std::string>();
    const std::optional<T> parsed = utils::TryParse<T>(raw, utils::ParseMode::Whole, 16);
    if(!parsed)
        ReportInvalidHex(element, raw, static_cast<int>(sizeof(T) * 2));
    return parsed;
}

// !\brief Read one optional child element into `out`, leaving it alone when the
// element is absent.
//
// These two lived in Utils.hpp, which meant every file that wanted
// utils::stoi compiled boost/property_tree as well. Five files use them.
template <typename T, typename U>
void ReadChildIfexists(const boost::property_tree::ptree::value_type& v, const std::string& child_name, U& out)
{
    auto child_val = v.second.get_child_optional(child_name);
    if(child_val)
        out = child_val->get_value<T>();
}

// !\brief Read one optional value into `out`, leaving it alone when absent.
template <typename A, typename U>
void ReadValueIfexists(A v, const std::string& child_name, U& out)
{
    auto child_val = v->template get_optional<U>(child_name);
    if(child_val)
        out = *child_val;
}
}

namespace utils
{
/* The settings reader spells the same helpers `utils::ini::` and some XML code
   spells them `utils::ptree::`; both have always meant utils::xml. */
namespace ini = xml;
namespace ptree = xml;
}
