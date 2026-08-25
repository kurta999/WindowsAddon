#pragma once

#include "utils/CSingleton.hpp"
#include "utils/SourceFileName.hpp"

#include <stdarg.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstring>
#include <ctime>
#include <deque>
#include <format>
#include <fstream>
#include <filesystem>
#include <mutex>
#include <source_location>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

#include "interface/ILogHelper.hpp"

#ifndef _WIN32
#include <fmt/format.h>
#include <fmt/chrono.h>
#endif

template<class> inline constexpr bool always_false_v = false;

// !\brief Narrow a log line for the byte-oriented sinks (log file, log panel).
// Log text is ASCII in practice; this makes the conversion explicit rather than
// relying on an implicit wchar_t -> char truncation.
template<class StringT>
[[nodiscard]] inline std::string NarrowLogText(const StringT& in)
{
    if constexpr(std::is_same_v<StringT, std::string>)
        return in;
    else
    {
        std::string out;
        out.reserve(in.size());
        for(const auto ch : in)
            out.push_back(static_cast<char>(ch));
        return out;
    }
}

#ifdef _WIN32
#define DBG(str, ...) \
    {\
        char __debug_format_str[64]; \
        snprintf(__debug_format_str, sizeof(__debug_format_str), str, __VA_ARGS__); \
        OutputDebugStringA(__debug_format_str); \
    }

#define DBGW(str, ...) \
    {\
        wchar_t __debug_format_str[128]; \
        wsprintfW(__debug_format_str, str, __VA_ARGS__); \
        OutputDebugStringW(__debug_format_str); \
    }
#else
#define DBG(str, ...) \
    { \
        fprintf(stderr, str, ##__VA_ARGS__); \
    }

#define DBGW(str, ...) \
    { \
        fwprintf(stderr, str, ##__VA_ARGS__); \
    }
#endif

enum LogLevel
{
    Debug,
    Verbose,
    Normal,
    Notification,
    Warning,
    Error,
    Critical
};

class LogEntry
{
public:
    LogEntry(std::string file_, std::string message_) :
        file(std::move(file_)), message(std::move(message_))
    {

    }

    std::string file;
    std::string message;
};

#define LOG_GUI_FORMAT "{:%Y.%m.%d %H:%M:%S} [{}] {}"
#define LOG_FILE_FORMAT "{:%Y.%m.%d %H:%M:%S} [{}] [{}:{} - {}] {}\n"

// !\brief The severity names as they appear in a log line.
//
// Was HelperTraits<std::string>::serverities, with a wide twin that existed
// only so that the whole of Emit could be instantiated for wchar_t.
inline constexpr std::string_view kLogSeverityNames[] = {
    "Debug", "Verbose", "Normal", "Notification", "Warning", "Error", "Critical" };

/* HelperTraits held the severity names, the two format strings and a pair of
   separator characters, once per character type, so that the whole of Emit
   could be instantiated wide as well as narrow. Only the vformat call actually
   needs the caller's character type; everything downstream of it writes bytes.
   The narrow copies of the severity names and the format strings live in
   Logger.cpp now, and the wide ones are gone with the wide instantiation. */

template <typename T> struct get_fmt_mkarg_type;
template <> struct get_fmt_mkarg_type<const wchar_t*> { using type = std::wformat_context; };
template <> struct get_fmt_mkarg_type<const wchar_t> { using type = std::wformat_context; };
template <> struct get_fmt_mkarg_type<wchar_t> { using type = std::wformat_context; };
template <> struct get_fmt_mkarg_type<const char*> { using type = std::format_context; };
template <> struct get_fmt_mkarg_type<const char> { using type = std::format_context; };
template <> struct get_fmt_mkarg_type<char> { using type = std::format_context; };

template <typename T> struct get_fmt_ret_string_type;
template <> struct get_fmt_ret_string_type<const wchar_t*> { using type = std::wstring; };
template <> struct get_fmt_ret_string_type<const wchar_t> { using type = std::wstring; };
template <> struct get_fmt_ret_string_type<wchar_t> { using type = std::wstring; };
template <> struct get_fmt_ret_string_type<const char*> { using type = std::string; };
template <> struct get_fmt_ret_string_type<const char> { using type = std::string; };
template <> struct get_fmt_ret_string_type<char> { using type = std::string; };

class SettingsReader;
class SettingsWriter;

class Logger : public CSingleton < Logger >
{
    friend class CSingleton < Logger >;
public:
    Logger();
    ~Logger() = default;

    void SetLogHelper(ILogHelper* helper);

    // !\brief Set default log level
    // !\param level [in] Log level
    void SetDefaultLogLevel(LogLevel level);

    // !\brief Get default log level
    // !\return Log level
    LogLevel GetDefaultLogLevel() const;

    // !\brief Set default log level as string
    // !\param level [in] Set log level as string
    // !\return Log level
    void SetLogLevelAsString(const std::string& level);

    // !\brief Get default log level as string
    const std::string GetLogLevelAsString() const;

    // !\brief Set log filters as string separated by | character
    // !\brief Read and write the two keys this logger owns.
    //
    // They live in the [App] block, where every settings.ini has them, so
    // Settings' binding calls these rather than spelling the logger's field
    // names itself - which was the other half of the Settings <-> Logger pair.
    void LoadSettingsFrom(SettingsReader& reader, std::string_view section);
    void WriteSettingsTo(SettingsWriter& writer) const;

    void SetLogFilters(const std::string& filter_list);

    // !\brief Get log filters as string separated by | character
    std::string GetLogFilters() const;

    // !\brief Execute search for a specific string in the log file
    // !\brief log_level Log level name in string format
    bool SearchInLogFile(std::string_view filter, std::string_view log_level);

    // !\brief Write one line, to the log file and to the pending queue.
    //
    // Was called LogInternal, which said where it sat rather than what it
    // does; LogM and LogW both forward here and there is nothing else it
    // could have meant.
    // !\brief Narrow the caller's message and hand it to EmitLine.
    //
    // Formatting the caller's arguments is the only part of writing a log
    // line that depends on their character type, so it is the only part that
    // stays a template. Everything after it writes bytes.
    template<class T, typename... Args>
    void Emit(LogLevel lvl, const std::source_location& location, std::basic_string_view<T> msg, Args &&...args)
    {
        using string_type = typename get_fmt_ret_string_type<T>::type;

        /* Both rejection tests read only the caller's arguments, so they run
           before any formatting: a suppressed line must not pay for vformat
           twice plus a time-zone lookup. */
        if(lvl < m_DefaultLogLevel)
            return;

        for(auto& i : m_LogFilters)
        {
            if(i.empty()) continue;
            if(std::search(msg.begin(), msg.end(), i.begin(), i.end()) != msg.end())
            {
                return;
            }
        }

        /* string_type(msg) rather than msg.data(): the message is a view, and
           the old spelling read from its pointer as though it were a C string. */
        const string_type formatted = (sizeof...(args) != 0)
            ? std::vformat(msg, std::make_format_args<typename get_fmt_mkarg_type<T>::type>(args...))
            : string_type(msg);

        EmitLine(lvl, location, NarrowLogText(formatted));
    }

    // !\brief Write one already-narrowed line to the log file and the
    // pending queue. Not a template: nothing below the vformat depends on the
    // character type the caller used, so this exists once instead of once per
    // character type.
    //
    // Still in the header rather than in Logger.cpp, deliberately. Logger.cpp
    // is linked into one target; the test targets substitute
    // tests/e2e/support/HeadlessLogger.cpp, and they get real formatting and
    // real file output precisely because that part is header-only. Moving this
    // body to Logger.cpp means a second copy of it in HeadlessLogger, which is
    // worse than what it would fix.
    void EmitLine(LogLevel lvl, const std::source_location& location,
        std::string_view formatted_msg)
    {
        const auto now = std::chrono::current_zone()->to_local(std::chrono::system_clock::now());
        const auto now_truncated_to_ms = std::chrono::floor<std::chrono::milliseconds>(now);
        std::string str = std::format(LOG_GUI_FORMAT, now_truncated_to_ms,
            kLogSeverityNames[lvl], formatted_msg);

        std::unique_lock lock(m_mutex);

        const std::string_view short_name = utils::BareFileName(location.file_name());

        if(lvl >= LogLevel::Verbose && lvl <= LogLevel::Critical)
        {
            fLog << std::format(LOG_FILE_FORMAT, now_truncated_to_ms,
                kLogSeverityNames[lvl], short_name, location.line(),
                location.function_name(), formatted_msg);
            fLog.flush();  /* File operation is handled directly here (at least for now - no time for fully async logger), it's not an expensive operation on modern SSDs */
        }

        /* Drained by AppendPreinitedEntries once a log view registers itself. A
           run without one - tests, CI, any headless service - never drains, so
           the queue keeps the most recent entries and drops the oldest instead
           of growing for the lifetime of the process. */
        if(preinit_entries.size() >= kMaxPendingEntries)
            preinit_entries.pop_front();
        preinit_entries.push_back({ std::string(short_name), std::move(str) });
    }

    template<typename... Args>
    void LogM(LogLevel lvl, const std::source_location& location, std::string_view msg, Args &&...args)
    {
        Emit(lvl, location, msg, std::forward<Args>(args)...);
    }

    template<typename... Args>
    void LogW(LogLevel lvl, const std::source_location& location, std::wstring_view msg, Args &&...args)
    {
        Emit(lvl, location, msg, std::forward<Args>(args)...);
    }

    // !\brief Write given log message to logfile.txt & LogPanel
    // !\param lvl [in] Serverity level
    // !\param file [in] Filename where the call comes from
    // !\param line [in] Line in source file where the call comes from
    // !\param function [in] Function name in source file where the call comes from
    // !\param msg [in] Message to log
    // !\param args [in] va_args arguments for std::format
    template<typename F = const char*, typename G = const char*, class T, typename... Args>
    void Log(LogLevel lvl, const std::source_location& location, T msg, Args &&...args)
    {
        using X = std::decay_t<decltype(msg)>;
        if constexpr(std::is_same_v<X, const char*>)
        {
            LogM(lvl, location, msg, std::forward<Args>(args)...);
        }
        else if constexpr(std::is_same_v<X, const wchar_t*>)
        {
            LogW(lvl, location, msg, std::forward<Args>(args)...);
        }
        else
        {
            static_assert(always_false_v<X>, "Invalid type. Only const char* and const wchar_t* are accepted!");
        }
    }
    // !\brief Append log messages to log panel which were logged before log panel was constructor
    void AppendPreinitedEntries();

    // !\brief Tick funciton
    void Tick();

private:
    LogLevel StringToLogLevel(const std::string& level);

    std::string LogLevelToString(LogLevel level) const;

    // !\brief Default log level
    LogLevel m_DefaultLogLevel = LogLevel::Verbose;

    // !\brief File handle for log 
    std::ofstream fLog;

    // !\brief Upper bound on undrained entries, see Emit.
    static constexpr std::size_t kMaxPendingEntries = 1000;

    // !\brief Log messages waiting to be published to the log view
    std::deque<LogEntry> preinit_entries;

    // !\brief Pointer to LogPanel
    ILogHelper* m_helper = nullptr;
    std::mutex m_helperMutex;

    // !\brief Log filters
    std::vector<std::string> m_LogFilters;

    // !\brief Logger's mutex
    std::mutex m_mutex;
};

/* Global LOG function */
template <typename... Ts>
struct LOG
{
    LOG(LogLevel lvl, Ts&&... ts, const std::source_location& loc = std::source_location::current())
    {
        Logger::Get()->Log(lvl, loc, std::forward<Ts>(ts)...);
    }
    ~LOG() = default;
};

template <typename... Ts>
LOG(LogLevel lvl, Ts&&...) -> LOG<Ts...>;
