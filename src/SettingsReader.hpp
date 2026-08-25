#pragma once

#include "SettingsSchema.hpp"

#include <boost/optional.hpp>
#include <boost/property_tree/ptree.hpp>

#include <format>
#include <functional>
#include <stdexcept>
#include <string>
#include <string_view>

// !\brief Read access to a parsed settings.ini, plus the context a failure
// message needs.
//
// This used to be a private helper inside Settings.cpp. It is the argument
// every ISettingsBinding receives, so it lives in a header where the subsystems
// that own their settings can reach it.
class SettingsReader
{
public:
    explicit SettingsReader(boost::property_tree::ptree& tree) : m_tree(tree) {}

    // !\brief The named section, or throws when it is absent.
    boost::property_tree::ptree& RequiredSection(std::string_view section)
    {
        m_section = section;
        m_key.clear();
        m_value.clear();

        auto child = m_tree.get_child_optional(m_section);
        if(!child)
            throw std::runtime_error(std::format("Required settings section [{}] is missing", m_section));
        return child.get();
    }

    // !\brief The named section if the file defines one.
    // Used by blocks that stay optional so older settings.ini files keep working.
    [[nodiscard]] boost::optional<boost::property_tree::ptree&> OptionalSection(std::string_view section)
    {
        m_section = section;
        m_key.clear();
        m_value.clear();
        return m_tree.get_child_optional(m_section);
    }

    // !\brief How many times the file defines this section. Numbered families
    // ("Backup_1", "Keys_Macro1") are walked with this.
    [[nodiscard]] std::size_t CountSection(const std::string& section) const
    {
        return m_tree.count(section);
    }

    // !\brief The value of a key that has to be present, or throws.
    std::string& Required(std::string_view section, std::string_view key)
    {
        /* The schema is what the shipped settings.ini is checked against, so a
           key read here but not declared there would silently stop being
           covered. */
        if(!settings_schema::IsDeclared(section, key) && undeclared_setting_reporter)
            undeclared_setting_reporter(section, key);

        auto& child = RequiredSection(section);
        m_key = key;
        auto value = child.find(m_key);
        if(value == child.not_found())
            throw std::runtime_error(std::format("Required setting [{}] {} is missing", m_section, m_key));

        m_value = value->second.data();
        return value->second.data();
    }

    void Track(std::string_view section, std::string_view key, std::string_view value)
    {
        m_section = section;
        m_key = key;
        m_value = value;
    }

    void TrackSection(std::string_view section)
    {
        m_section = section;
        m_key.clear();
        m_value.clear();
    }

    // !\brief What was last being read, for a failure message.
    [[nodiscard]] std::string Context() const
    {
        if(m_section.empty())
            return "settings initialization";
        if(m_key.empty())
            return std::format("section [{}]", m_section);

        constexpr size_t max_value_length = 160;
        std::string displayed_value = m_value;
        if(displayed_value.size() > max_value_length)
        {
            displayed_value.resize(max_value_length);
            displayed_value += "...";
        }
        return std::format("setting [{}] {} (value: '{}')", m_section, m_key, displayed_value);
    }

    // !\brief Where "this key is not in the schema" is reported.
    // A hook rather than a direct log call, so reading settings does not drag
    // the logger into every target that owns settings.
    static inline std::function<void(std::string_view, std::string_view)> undeclared_setting_reporter;

private:
    boost::property_tree::ptree& m_tree;
    std::string m_section;
    std::string m_key;
    std::string m_value;
};
