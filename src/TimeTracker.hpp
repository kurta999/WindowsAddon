#pragma once

#include "utils/CSingleton.hpp"
#include <atomic>
#include <condition_variable>
#include <string>
#include <map>

#include "IDatabase.hpp" 

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

    int total_worktime { 0 };  /* Seconds */

    std::vector<std::unique_ptr<TimeEntry>> entries;
    std::map<int, std::vector<std::unique_ptr<TimeEntrySerialized>>> serialized_entries;  /* [day] = TimeEntrySerialized */
};

class TimeTracker
{
public:
    TimeTracker();
    ~TimeTracker();

    // !\brief Initialize sensors
    void Init();

    void LoadEntries(int year, int month);
    void LoadEntriesForOneMonth(int year, int month);
    void UpdateEntries();

    TimeEntry* AddEntry(boost::posix_time::ptime start, boost::posix_time::ptime end, const std::string& comment = "", int sqlid = 0);
	void EditEntry(TimeEntry* entry, boost::posix_time::ptime start, boost::posix_time::ptime end, const std::string& comment = "");
    void SaveEntry(TimeEntry* entry);
    void RemoveEntry(TimeEntry* entry);

    std::map<int, std::unique_ptr<MonthlyTimeEntry>>& GetEntries() { return m_Entries; }

	void SetHourlyRate(int rate) { hourly_rate = rate; }
	int GetHourlyRate() { return hourly_rate; }

    void SetToggleKey(const std::string& key) { toggle_key = key; }
    const std::string GetToggleKey() { return toggle_key; }

    int GetLastId() { return m_LastId; }

    int CalculateMapDateOffset(boost::posix_time::ptime start);

    void SetActualYearMonth(int year, int month)
    {
        m_Year = year;
        m_Month = month;
    }

    int m_Year = 0;

    int m_Month = 0;
private:

    void Query_OneMonth(std::unique_ptr<Result>& result, std::any param);
    void SerializeEntriesForOneMonth(int year, int month);
    void ReorganizeSerializedEntries(std::map<int, std::vector<std::unique_ptr<TimeEntrySerialized>>>& serialized_entries);

    std::map<int, std::unique_ptr<MonthlyTimeEntry>> m_Entries;  /* [year + month] = MonthlyTimeEntry */
    boost::gregorian::date today;

    // !\brief Pointer to database
    std::unique_ptr<IDatabase> m_db{ nullptr };

    // !\brief Future for executing async database read for graph generation
    std::future<void> m_graph_future;

    int hourly_rate = 10;

    std::string toggle_key = "G14";

    int m_LastId = 0;

    bool m_destructing = false;
};