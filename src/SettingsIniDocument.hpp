#pragma once

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

class SettingsIniDocument
{
public:
    struct Entry
    {
        std::string section;
        std::string key;
        std::string value;
        std::string comment;
        std::size_t line_index = 0;
        std::size_t value_begin = 0;
        std::size_t value_end = 0;
    };

    bool Parse(std::string content, std::string& error);
    std::string Render() const;

    const std::vector<Entry>& Entries() const { return m_entries; }
    std::vector<Entry>& Entries() { return m_entries; }

    static bool IsEditableSection(std::string_view section);

private:
    struct Line
    {
        std::string content;
        std::string ending;
        std::size_t entry_index = static_cast<std::size_t>(-1);
    };

    std::vector<Line> m_lines;
    std::vector<Entry> m_entries;
};
