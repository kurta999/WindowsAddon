#pragma once

#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>
#include <boost/date_time/posix_time/ptime.hpp>

#include "interface/ITimeTrackerStorage.hpp"
#include "TimeTrackerLogic.hpp"

class TimeEntry
{
public:
    TimeEntry(boost::posix_time::ptime _start, boost::posix_time::ptime _end, const std::string& desc_, int sql_id_) :
        start(_start), end(_end), desc(desc_), sql_id(sql_id_)
    {

    }

    boost::posix_time::ptime start;
    boost::posix_time::ptime end;
    std::string desc;
    int sql_id;
    bool is_overlap{ false };
};

class TimeGroupEntry
{
public:
    TimeGroupEntry(const std::string& _name, uint32_t _color, int _sql_id) :
        name(_name), color(_color), sql_id(_sql_id)
    {

    }

    std::string name;
    uint32_t color;
    int sql_id;
};

class TimeEntrySerialized
{
public:
    TimeEntrySerialized(boost::posix_time::time_duration _start, boost::posix_time::time_duration _end, 
        boost::posix_time::time_duration _work_duration, TimeEntry* _entry) :
        start(_start), end(_end), work_duration(_work_duration), entry(_entry)
    {

    }

	boost::gregorian::date date;
	boost::posix_time::time_duration start;
	boost::posix_time::time_duration end;
    boost::posix_time::time_duration work_duration;
    boost::posix_time::time_duration total_duration_per_day;
	TimeEntry* entry;
    bool is_overlap{ false };
    bool is_invalid{ false };
    bool is_time_bad{ false };
    bool is_white{ false };
};

class MonthlyTimeEntry
{
public:
    MonthlyTimeEntry() = default;

    std::int64_t total_worktime { 0 };  /* Seconds */

    std::vector<std::unique_ptr<TimeEntry>> entries;
    std::map<int, std::vector<std::unique_ptr<TimeEntrySerialized>>> serialized_entries;  /* [day] = TimeEntrySerialized */
};

class TimeTracker
{
public:
    using ErrorHandler = std::function<void(const std::string&)>;

    explicit TimeTracker(std::unique_ptr<ITimeTrackerStorage> storage, ErrorHandler error_handler = {});
    ~TimeTracker() = default;

    // !\brief Initialize the time tracker and load current month entries
    void Init();

    void LoadEntries(int year, int month);
    void LoadEntriesForOneMonth(int year, int month);
    void LoadGroups();
    void UpdateEntries();

    TimeEntry* AddEntry(boost::posix_time::ptime start, boost::posix_time::ptime end, const std::string& comment = "", int sqlid = 0);
    [[nodiscard]] TimeEntry* FindEntry(int sql_id);
    [[nodiscard]] const TimeEntry* FindEntry(int sql_id) const;
    [[nodiscard]] bool EditEntry(int sql_id, boost::posix_time::ptime start, boost::posix_time::ptime end, const std::string& comment = "");
    [[nodiscard]] bool SaveEntry(int sql_id);
    [[nodiscard]] bool RemoveEntry(int sql_id);
    [[nodiscard]] const std::string& LastError() const { return m_last_error; }

    std::map<int, std::unique_ptr<MonthlyTimeEntry>>& GetEntries() { return m_Entries; }

    void SetHourlyRate(int rate) { hourly_rate = rate; }
    int GetHourlyRate() const { return hourly_rate; }

    void SetToggleKey(const std::string& key) { toggle_key = key; }
    const std::string& GetToggleKey() const { return toggle_key; }

    int GetLastId() const { return m_LastId; }

    int CalculateMapDateOffset(const boost::posix_time::ptime& start);

    void SetActualYearMonth(int year, int month)
    {
        m_Year = year;
        m_Month = month;
    }

    int GetYear() const { return m_Year; }
    int GetMonth() const { return m_Month; }

private:
    int m_Year = 0;
    int m_Month = 0;

    void SerializeEntriesForOneMonth(int year, int month);
    bool MoveEntryToMonth(int sql_id, int new_map_key);
    void ReportError(std::string message);

    std::map<int, std::unique_ptr<MonthlyTimeEntry>> m_Entries;  /* [YYYYMM] = MonthlyTimeEntry */
    std::map<int, std::unique_ptr<TimeGroupEntry>> m_Groups;     /* [id]    = TimeGroupEntry */
    boost::gregorian::date today;

    std::unique_ptr<ITimeTrackerStorage> m_storage;
    ErrorHandler m_error_handler;
    std::string m_last_error;

    int hourly_rate = 10;

    std::string toggle_key = "G14";

    int m_LastId = 0;
};
