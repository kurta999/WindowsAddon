#include "Logger.hpp"
#include "LogFileSearch.hpp"

#include <boost/algorithm/string.hpp>

#include <array>
#include <cassert>
#include <cstdio>
#include <fstream>
#include <string>
#include <utility>
#include "SettingsReader.hpp"
#include "SettingsWriter.hpp"

constexpr const char* LOG_FILENAME = "./logfile.txt";

namespace
{
/* One table for both directions. Two independent if-chains used to encode the
   same mapping, so a level could be parsed and then printed as something else. */
constexpr std::array<std::pair<LogLevel, std::string_view>, 7> kLogLevelNames{{
    {LogLevel::Debug, "Debug"},
    {LogLevel::Verbose, "Verbose"},
    {LogLevel::Normal, "Normal"},
    {LogLevel::Notification, "Notification"},
    {LogLevel::Warning, "Warning"},
    {LogLevel::Error, "Error"},
    {LogLevel::Critical, "Critical"},
}};
}

Logger::Logger()
{
    fLog.open(LOG_FILENAME, std::ofstream::binary);
    assert(fLog);
}

void Logger::SetLogHelper(ILogHelper* helper)
{
    std::scoped_lock lock(m_helperMutex);
    m_helper = helper;
}

void Logger::SetDefaultLogLevel(LogLevel level)
{
	m_DefaultLogLevel = level;
}

LogLevel Logger::GetDefaultLogLevel() const
{
	return m_DefaultLogLevel;
}

void Logger::LoadSettingsFrom(SettingsReader& reader, std::string_view section)
{
	const std::string block(section);
	SetLogLevelAsString(reader.Required(block, "DefaultLogLevel"));
	SetLogFilters(reader.Required(block, "LogFilters"));
}

void Logger::WriteSettingsTo(SettingsWriter& writer) const
{
	writer.Key("DefaultLogLevel", GetLogLevelAsString())
		.Key("LogFilters", GetLogFilters());
}

void Logger::SetLogLevelAsString(const std::string& level)
{
	m_DefaultLogLevel = StringToLogLevel(level);
}

const std::string Logger::GetLogLevelAsString() const
{
	const std::string ret = LogLevelToString(m_DefaultLogLevel);
	return ret;
}

void Logger::SetLogFilters(const std::string& filter_list)
{
	m_LogFilters.clear();
	std::vector<std::string> filters;
	boost::split(filters, filter_list, [](char input) { return input == '|'; }, boost::algorithm::token_compress_on);
	if(!filters.empty())
	{
		for(auto& i : filters)
		{
			if(i.empty()) continue;
			m_LogFilters.push_back(i);
		}
	}
}

std::string Logger::GetLogFilters() const
{
	std::string ret;
	for(auto& i : m_LogFilters)
	{
		ret += i + "|";
	}

	if(!ret.empty() && ret.back() == '|')
		ret.pop_back();
	return ret;
}

bool Logger::SearchInLogFile(std::string_view filter, std::string_view log_level)
{
	std::ifstream in(LOG_FILENAME);
	if(!in)
	{
		LOG(LogLevel::Error, "Failed to open log file ({}) for search", LOG_FILENAME);
		return false;
	}
	std::scoped_lock helper_lock(m_helperMutex);
	if(!m_helper)
	{
		LOG(LogLevel::Error, "m_helper is nullptr");
		return false;
	}

	/* Splitting a line into its fields is log_file::Parse, which has no
	   buffers to overrun - the sscanf this replaced skipped any line whose
	   function signature was longer than 255 characters, which in a recent
	   test run was twelve lines out of sixty-five. */
	m_helper->ClearEntries();
	std::string line;
	while(std::getline(in, line, '\n'))
	{
		const auto parsed = log_file::Parse(line);
		if(!parsed || !log_file::Matches(*parsed, filter, log_level))
			continue;

		m_helper->AppendLog(std::string(parsed->file), line);
	}
	return true;
}

void Logger::AppendPreinitedEntries()
{
	/* A registered helper is what "the log view exists" means here. Looking the
	   frame up as well only re-asked the same question through the GUI. */
	std::scoped_lock helper_lock(m_helperMutex);
	if(!m_helper)
		return;

	std::unique_lock lock(m_mutex);
	for(auto& i : preinit_entries)
	{
		if(!i.message.empty())
			m_helper->AppendLog(i.file, i.message, true);
	}
	preinit_entries.clear();
}

void Logger::Tick()
{
	AppendPreinitedEntries();
}

LogLevel Logger::StringToLogLevel(const std::string& level)
{
	for(const auto& [value, name] : kLogLevelNames)
	{
		if(boost::algorithm::icontains(level, name))
			return value;
	}
	LOG(LogLevel::Error, "Invalid log level: {}", level);
	return LogLevel::Verbose;
}

std::string Logger::LogLevelToString(LogLevel level) const
{
	for(const auto& [value, name] : kLogLevelNames)
	{
		if(value == level)
			return std::string(name);
	}
	LOG(LogLevel::Error, "Invalid log level: {}", static_cast<int>(level));
	return "Verbose";
}
