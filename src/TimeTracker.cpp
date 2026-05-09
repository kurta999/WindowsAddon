#include "pch.hpp"

namespace {
    constexpr const char*  db_name       = "time_db.db";
    constexpr int64_t      SecondsPerDay = 86400;

    uint64_t secondsSinceEpoch(const boost::posix_time::ptime& time)
    {
        namespace bpt = boost::posix_time;
        const bpt::ptime epoch = bpt::from_time_t(0);
        return (time - epoch).total_seconds();
    }
}

TimeTracker::TimeTracker()
{

}

TimeTracker::~TimeTracker()
{
    m_destructing = true;
}

void TimeTracker::Init()
{
    m_db = std::make_unique<Sqlite3Database>();

    today = boost::posix_time::second_clock::local_time().date();
    int current_year = today.year();
    int current_month = today.month();
    m_Year = current_year;
    m_Month = current_month;

    m_Entries.clear();
    LoadEntriesForOneMonth(current_year, current_month);
    SerializeEntriesForOneMonth(current_year, current_month);
}

void TimeTracker::LoadEntries(int year, int month)
{
    m_Entries.clear();
    LoadEntriesForOneMonth(year, month);
    SerializeEntriesForOneMonth(year, month);
}

void TimeTracker::LoadEntriesForOneMonth(int year, int month)
{
    boost::gregorian::date first_day(year, month, 1);
    boost::gregorian::date last_day = first_day.end_of_month();

    boost::posix_time::ptime epoch(boost::gregorian::date(1970, 1, 1));
    int64_t first_timestamp = (first_day - epoch.date()).days() * SecondsPerDay;
    int64_t last_timestamp  = (last_day  - epoch.date()).days() * SecondsPerDay + (SecondsPerDay - 1);

    std::string sql = std::format("SELECT id, start, end, comment FROM time_table WHERE start >= {} AND start <= {} ORDER BY start ASC;", first_timestamp, last_timestamp);

    DBStream db_stream(db_name, *m_db);
    if (!db_stream)
    {
        LOG(LogLevel::Critical, "Failed to open the database for time tracker!");
        return;
    }

    db_stream.SendQueryAndFetch(sql,
        [this, year, month](std::unique_ptr<Result>& result) { Query_OneMonth(result, year, month); });
}

void TimeTracker::LoadGroups()
{
    // Not yet implemented
}

void TimeTracker::UpdateEntries()
{
    SerializeEntriesForOneMonth(m_Year, m_Month);
}

void TimeTracker::SerializeEntriesForOneMonth(int year, int month)
{
    boost::gregorian::date date(year, month, 1);
    boost::posix_time::ptime month_start(date);
    int offset = CalculateMapDateOffset(month_start);
    auto it = m_Entries.find(offset);
    if (it == m_Entries.end())
        return;

    it->second->serialized_entries.clear();
    it->second->total_worktime = 0;

    // First pass: group entries by day and compute overlap flags
    std::map<int, boost::posix_time::time_duration> day_totals;
    std::map<int, std::vector<TimeEntry*>> day_entries;
    for (auto& entry : it->second->entries)
    {
        int day_number = entry->start.date().day().as_number();
        day_entries[day_number].emplace_back(entry.get());
    }

    for (auto& [day, entries] : day_entries)
    {
        std::sort(entries.begin(), entries.end(),
            [](const TimeEntry* a, const TimeEntry* b) { return a->start < b->start; });

        for (auto entry : entries)
            entry->is_overlap = false;

        // Check all pairs — sorted order means we can break early when no overlap is possible
        for (size_t i = 0; i < entries.size(); ++i)
        {
            for (size_t j = i + 1; j < entries.size(); ++j)
            {
                if (entries[i]->end <= entries[j]->start)
                    break;

                DBG("OVERLAP DETECTED on day %d between:\n", day);
                DBG("  Entry 1: %s - %s\n",
                    boost::posix_time::to_simple_string(entries[i]->start).c_str(),
                    boost::posix_time::to_simple_string(entries[i]->end).c_str());
                DBG("  Entry 2: %s - %s\n",
                    boost::posix_time::to_simple_string(entries[j]->start).c_str(),
                    boost::posix_time::to_simple_string(entries[j]->end).c_str());
                DBG("----\n");

                entries[i]->is_overlap = true;
                entries[j]->is_overlap = true;
            }
        }
    }

    // Second pass: accumulate non-overlapping durations per day
    for (auto& entry : it->second->entries)
    {
        const auto duration = entry->end - entry->start;
        if (!entry->is_overlap && duration.is_positive())
            day_totals[entry->start.date().day().as_number()] += duration;
    }

    // Third pass: build serialized view
    std::set<int> processed_days;
    bool is_white = true;
    boost::gregorian::date current_date;
    for (auto& entry : it->second->entries)
    {
        const boost::gregorian::date entry_date = entry->start.date();
        bool is_set_date = false;

        if (entry_date != current_date)
        {
            is_set_date = true;
            current_date = entry_date;
            is_white = !is_white;
        }

        auto serialized_entry = std::make_unique<TimeEntrySerialized>(
            entry->start.time_of_day(), entry->end.time_of_day(), entry->end - entry->start, entry.get());

        if (is_set_date)
            serialized_entry->date = entry_date;

        serialized_entry->is_white = is_white;
        if (serialized_entry->start > serialized_entry->end)
            serialized_entry->is_time_bad = true;

        const int day_number = entry_date.day();
        if (!processed_days.contains(day_number) && !entry->is_overlap)
        {
            serialized_entry->total_duration_per_day = day_totals[day_number];
            it->second->total_worktime += serialized_entry->total_duration_per_day.total_seconds();
            processed_days.insert(day_number);
        }
        else
        {
            serialized_entry->total_duration_per_day = boost::posix_time::time_duration();
        }

        it->second->serialized_entries[day_number].push_back(std::move(serialized_entry));
    }
}

TimeEntry* TimeTracker::AddEntry(boost::posix_time::ptime start, boost::posix_time::ptime end, const std::string& comment, int sqlid)
{
    DBStream db_stream(db_name, *m_db);
    if (!db_stream)
    {
        LOG(LogLevel::Critical, "Failed to open the database for time tracker!");
        return nullptr;
    }

    if (sqlid == 0)
    {
        char table_query[] = "CREATE TABLE IF NOT EXISTS time_table(\
id integer primary key,\
start   INT NOT NULL,\
end     INT     NOT NULL,\
comment VARCHAR(256)  NOT NULL);";
        db_stream.ExecuteQuery(table_query);
        const std::string query = std::format("INSERT INTO time_table(start, end, comment) VALUES({}, 0, '{}')", secondsSinceEpoch(start), comment);
        m_LastId = db_stream.ExecuteQueryAndGetLastId(query);
        sqlid = m_LastId;
    }

    std::unique_ptr<TimeEntry> entry = std::make_unique<TimeEntry>(start, end, comment, sqlid);

    int map_key = CalculateMapDateOffset(start);
    DBG("Adding entry with map key: %d\n", map_key);
    auto [it, inserted] = m_Entries.try_emplace(map_key, std::make_unique<MonthlyTimeEntry>());

    it->second->entries.push_back(std::move(entry));
    auto ret = it->second->entries.back().get();
    return ret;
}

void TimeTracker::EditEntry(TimeEntry* entry, boost::posix_time::ptime start, boost::posix_time::ptime end, const std::string& comment)
{
	if (entry == nullptr)
		return;
	DBStream db_stream(db_name, *m_db);
	if (!db_stream)
	{
		LOG(LogLevel::Critical, "Failed to open the database for time tracker!");
		return;
	}
	std::string query = std::format("UPDATE time_table SET start = {}, end = {}, comment = '{}' WHERE id = {}",
		secondsSinceEpoch(start), secondsSinceEpoch(end), comment, entry->sql_id);
	db_stream.ExecuteQuery(query);
	entry->start = start;
	entry->end = end;
	entry->desc = comment;
}

void TimeTracker::SaveEntry(TimeEntry* entry)
{
	if (entry == nullptr)
		return;
	DBStream db_stream(db_name, *m_db);
	if (!db_stream)
	{
		LOG(LogLevel::Critical, "Failed to open the database for time tracker!");
		return;
	}
    std::string query = std::format("UPDATE time_table SET start = {}, end = {}, comment = '{}' WHERE id = {}",
        secondsSinceEpoch(entry->start), secondsSinceEpoch(entry->end), entry->desc, entry->sql_id);
    db_stream.ExecuteQuery(query);
}

void TimeTracker::RemoveEntry(TimeEntry* entry)
{
	if (entry == nullptr)
		return;
	DBStream db_stream(db_name, *m_db);
	if (!db_stream)
	{
		LOG(LogLevel::Critical, "Failed to open the database for time tracker!");
		return;
	}
	std::string query = std::format("DELETE FROM time_table WHERE id = {}", entry->sql_id);
	db_stream.ExecuteQuery(query);
	int map_key = CalculateMapDateOffset(entry->start);
	auto it = m_Entries.find(map_key);
	if (it != m_Entries.end())
	{
		auto& entries = it->second->entries;
		entries.erase(std::remove_if(entries.begin(), entries.end(),
			[entry](const std::unique_ptr<TimeEntry>& e) { return e.get() == entry; }), entries.end());
	}
}

int TimeTracker::CalculateMapDateOffset(const boost::posix_time::ptime& start)
{
    const boost::gregorian::date date = start.date();
    return date.year() * 100 + date.month();
}

void TimeTracker::Query_OneMonth(std::unique_ptr<Result>& result, int year, int month)
{
    while (!m_destructing)
    {
        if (!result->StepNext())
            break;

        int            id    = result->GetColumnInt(0);
        int64_t        start = result->GetColumnInt(1);
        int64_t        end   = result->GetColumnInt(2);
        std::string desc_str{result->GetColumnText(3)};
        AddEntry(boost::posix_time::from_time_t(start), boost::posix_time::from_time_t(end), desc_str, id);
    }

    SerializeEntriesForOneMonth(year, month);
}
