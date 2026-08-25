#include "SettingsIniDocument.hpp"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <limits>

#ifdef _WIN32
/* MoveFileExW: std::filesystem::rename maps to it, but without
   MOVEFILE_WRITE_THROUGH, which is the half that makes the replace durable.
   NOMINMAX because this target does not define it globally and this file
   uses std::numeric_limits<...>::max(). */
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#endif

namespace
{
std::string_view Trim(std::string_view value)
{
    while(!value.empty() && std::isspace(static_cast<unsigned char>(value.front())))
        value.remove_prefix(1);
    while(!value.empty() && std::isspace(static_cast<unsigned char>(value.back())))
        value.remove_suffix(1);
    return value;
}

std::string Normalize(std::string_view value)
{
    std::string normalized;
    normalized.reserve(value.size());
    for(const unsigned char character : value)
    {
        if(character == '_' || character == '-' || std::isspace(character))
            continue;
        normalized.push_back(static_cast<char>(std::tolower(character)));
    }
    return normalized;
}

std::size_t FindInlineComment(std::string_view line, std::size_t begin)
{
    for(std::size_t index = begin; index < line.size(); ++index)
    {
        if((line[index] == '#' || line[index] == ';') &&
            (index == begin || std::isspace(static_cast<unsigned char>(line[index - 1]))))
        {
            return index;
        }
    }
    return std::string_view::npos;
}
}

bool SettingsIniDocument::IsEditableSection(std::string_view section)
{
    const std::string normalized = Normalize(section);
    if(normalized == "macroconfig" || normalized == "keysglobal" || normalized.starts_with("keysmacro"))
        return false;
    if(normalized == "backupsettings" || normalized.starts_with("backup"))
        return false;
    if(normalized == "cmdexecutor" || normalized == "commandexecutor")
        return false;
    return !normalized.empty();
}

bool SettingsIniDocument::Parse(std::string content, std::string& error)
{
    m_lines.clear();
    m_entries.clear();
    error.clear();

    std::size_t offset = 0;
    while(offset < content.size())
    {
        const std::size_t line_end = content.find_first_of("\r\n", offset);
        Line line;
        if(line_end == std::string::npos)
        {
            line.content = content.substr(offset);
            offset = content.size();
        }
        else
        {
            line.content = content.substr(offset, line_end - offset);
            if(content[line_end] == '\r' && line_end + 1 < content.size() && content[line_end + 1] == '\n')
            {
                line.ending = "\r\n";
                offset = line_end + 2;
            }
            else
            {
                line.ending.assign(1, content[line_end]);
                offset = line_end + 1;
            }
        }
        m_lines.push_back(std::move(line));
    }

    if(content.empty())
    {
        error = "settings.ini is empty";
        return false;
    }

    std::string current_section;
    for(std::size_t line_index = 0; line_index < m_lines.size(); ++line_index)
    {
        const std::string_view line = m_lines[line_index].content;
        const std::string_view trimmed = Trim(line);
        if(trimmed.empty() || trimmed.front() == '#' || trimmed.front() == ';')
            continue;

        if(trimmed.front() == '[')
        {
            const std::size_t closing = trimmed.find(']');
            if(closing == std::string_view::npos)
            {
                error = "Malformed section header at line " + std::to_string(line_index + 1);
                return false;
            }
            current_section = std::string(Trim(trimmed.substr(1, closing - 1)));
            continue;
        }

        if(current_section.empty() || !IsEditableSection(current_section))
            continue;

        const std::size_t equals = line.find('=');
        if(equals == std::string_view::npos)
            continue;

        const std::string_view key = Trim(line.substr(0, equals));
        if(key.empty())
            continue;

        std::size_t value_begin = equals + 1;
        while(value_begin < line.size() && std::isspace(static_cast<unsigned char>(line[value_begin])))
            ++value_begin;

        const std::size_t comment_begin = FindInlineComment(line, value_begin);
        std::size_t value_end = comment_begin == std::string_view::npos ? line.size() : comment_begin;
        while(value_end > value_begin && std::isspace(static_cast<unsigned char>(line[value_end - 1])))
            --value_end;

        Entry entry;
        entry.section = current_section;
        entry.key = std::string(key);
        entry.value = std::string(line.substr(value_begin, value_end - value_begin));
        if(comment_begin != std::string_view::npos)
            entry.comment = std::string(Trim(line.substr(comment_begin + 1)));
        entry.line_index = line_index;
        entry.value_begin = value_begin;
        entry.value_end = value_end;

        m_lines[line_index].entry_index = m_entries.size();
        m_entries.push_back(std::move(entry));
    }

    if(m_entries.empty())
    {
        error = "settings.ini does not contain editable settings";
        return false;
    }
    return true;
}

std::string SettingsIniDocument::Render() const
{
    std::string rendered;
    for(const Line& line : m_lines)
    {
        if(line.entry_index == std::numeric_limits<std::size_t>::max())
        {
            rendered += line.content;
        }
        else
        {
            const Entry& entry = m_entries[line.entry_index];
            rendered.append(line.content, 0, entry.value_begin);
            rendered += entry.value;
            rendered.append(line.content, entry.value_end, std::string::npos);
        }
        rendered += line.ending;
    }
    return rendered;
}

bool SettingsIniDocument::LoadFromFile(const std::filesystem::path& path, std::string& error)
{
    std::ifstream input(path, std::ios::binary);
    if(!input.is_open())
    {
        error = "Unable to open " + path.generic_string();
        return false;
    }
    std::string content{std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
    return Parse(std::move(content), error);
}

bool SettingsIniDocument::SaveToFileAtomically(const std::filesystem::path& path, std::string& error) const
{
    const std::filesystem::path settings_path = std::filesystem::absolute(path).lexically_normal();
    std::filesystem::path temporary_path = settings_path;
    temporary_path += ".tmp";

    std::ofstream output(temporary_path, std::ios::binary | std::ios::trunc);
    if(!output.is_open())
    {
        error = "Unable to open the temporary settings file for writing";
        return false;
    }
    output << Render();
    output.flush();
    if(!output)
    {
        error = "Failed while writing the temporary settings file";
        output.close();
        std::error_code remove_error;
        std::filesystem::remove(temporary_path, remove_error);
        return false;
    }
    output.close();

#ifdef _WIN32
    if(!MoveFileExW(temporary_path.c_str(), settings_path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
    {
        error = "Unable to replace " + settings_path.filename().generic_string()
            + " (Windows error " + std::to_string(GetLastError()) + ")";
        std::error_code remove_error;
        std::filesystem::remove(temporary_path, remove_error);
        return false;
    }
#else
    std::error_code rename_error;
    std::filesystem::rename(temporary_path, settings_path, rename_error);
    if(rename_error)
    {
        error = "Unable to replace " + settings_path.filename().generic_string()
            + ": " + rename_error.message();
        std::error_code remove_error;
        std::filesystem::remove(temporary_path, remove_error);
        return false;
    }
#endif
    return true;
}
