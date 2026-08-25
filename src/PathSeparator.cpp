#include "pch_core.hpp"
#include "PathSeparator.hpp"
#include "Logger.hpp"
#include "SettingsReader.hpp"
#include "SettingsWriter.hpp"
#include <boost/algorithm/string/replace.hpp>
#include <ostream>

void PathSeparator::OnHotkeyPressed()
{
    ReplaceClipboard(ReplaceType::PATH_SEPARATOR);
}

std::string PathSeparator::Replace(std::string input, ReplaceType type)
{
    switch(type)
    {
        case ReplaceType::PATH_SEPARATOR:
            ReplaceString(input);
            break;
        case ReplaceType::WSL:
            ReplaceStringFromWindowsToWsl(input);
            break;
    }
    return input;
}

void PathSeparator::ReplaceClipboard(ReplaceType type)
{
    if(!m_Clipboard)
    {
        LOG(LogLevel::Error, "No clipboard is wired up; path separators were not replaced");
        return;
    }

    auto text = m_Clipboard->GetText();
    if(!text)
        return;

    std::string replaced = Replace(std::move(*text), type);
    if(!m_Clipboard->SetText(replaced))
        return;

    if(m_Sink)
        m_Sink->PostNotification(PathSeparatorsReplacedNotification{std::move(replaced)});
}

void PathSeparator::ReplaceString(std::string& str)
{
	if(str.find('\\') != std::string::npos)
		boost::algorithm::replace_all(str, "\\", "/");
	else
		boost::algorithm::replace_all(str, "/", "\\");
}

void PathSeparator::ReplaceStringFromWindowsToWsl(std::string& str)
{
	if(str.find("/mnt/") != std::string::npos)
	{
		boost::algorithm::replace_all(str, "/", "\\");
		str.erase(0, 5); // strip leading \mnt\ prefix (slashes already replaced above)
		if(!str.empty())
		{
			str[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(str[0])));
			str.insert(1, ":");
		}
	}
	else
	{
		boost::algorithm::replace_all(str, "\\", "/");
	}
}

void PathSeparator::LoadSettings(SettingsReader& reader)
{
    replace_key = reader.Required("PathSeparator", "ReplacePathSeparatorKey");
}

void PathSeparator::SaveSettings(std::ostream& out) const
{
    SettingsWriter(out, "PathSeparator")
        .Key("ReplacePathSeparatorKey", replace_key)
        .Blank();
}
