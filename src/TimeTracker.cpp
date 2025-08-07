#include "pch.hpp"

constexpr const char* db_name = "time_db.db";

uint64_t secondsSinceEpoch(const boost::posix_time::ptime& time)
{
    namespace bpt = boost::posix_time;
    const bpt::ptime epoch = bpt::from_time_t(0);
    bpt::time_duration duration = time - epoch;
    return duration.total_seconds();
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
    //AddEntry(boost::posix_time::second_clock::local_time(), "fasz");
    
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
    // Get first and last day of the given month
    boost::gregorian::date first_day(year, month, 1);
    boost::gregorian::date last_day = first_day.end_of_month();

    // Convert to UNIX timestamps
    boost::posix_time::ptime epoch(boost::gregorian::date(1970, 1, 1));
    int first_timestamp = (first_day - epoch.date()).days() * 86400; // Start of month (00:00:00)
    int last_timestamp = (last_day - epoch.date()).days() * 86400 + 86399; // End of last day (23:59:59)

    // Format SQL query using std::format (C++20)
    std::string sql = std::format("SELECT id, start, end, comment FROM time_table WHERE start >= {} AND start <= {} ORDER BY start ASC;", first_timestamp, last_timestamp);

    DBStream db_stream(db_name, m_db);
    if (!db_stream)
    {
        LOG(LogLevel::Critical, "Failed to open the database for measurements!");
        return;
    }

    db_stream.SendQueryAndFetch(sql, std::bind(&TimeTracker::Query_OneMonth, this, std::placeholders::_1, std::placeholders::_2), std::pair<int, int>(year, month));
}

void TimeTracker::UpdateEntries()
{
    SerializeEntriesForOneMonth(m_Year, m_Month);
}

void TimeTracker::ReorganizeSerializedEntries(std::map<int, std::vector<std::unique_ptr<TimeEntrySerialized>>>& serialized_entries)
{
    return;
    for (auto& [key, entries] : serialized_entries)
    {
        // Step 1: Sort by date (descending), then start time (ascending)
        std::sort(entries.begin(), entries.end(),
            [](const std::unique_ptr<TimeEntrySerialized>& a, const std::unique_ptr<TimeEntrySerialized>& b)
            {
                if (a->date != b->date)
                    return a->date > b->date; // Newest date first
                return a->start < b->start;    // Earlier start time first within the same day
            });

        // Step 2: Correct the date visibility
        boost::gregorian::date last_seen_date;
        bool is_first_entry_for_day = true;

        for (auto& entry : entries)
        {
            if (entry->date != last_seen_date)
            {
                last_seen_date = entry->date;
                is_first_entry_for_day = true;
            }

            if (is_first_entry_for_day)
            {
                is_first_entry_for_day = false;
            }
            else
            {
                entry->date = boost::gregorian::date{};
            }
        }
    }
}

void TimeTracker::SerializeEntriesForOneMonth(int year, int month)
{
    boost::gregorian::date date(year, month, 1);
    boost::posix_time::ptime month_start(date);
    int offset = CalculateMapDateOffset(month_start);
    if (m_Entries.empty())
        return;
    auto it = m_Entries.find(offset);

    it->second->serialized_entries.clear();
	it->second->total_worktime = 0;

    if (it != m_Entries.end())
    {
        // Keep track of days we've already processed for total hours
        std::set<int> processed_days;

        // First pass: calculate total duration for each day
        std::map<int, boost::posix_time::time_duration> day_totals;
        std::map<int, std::vector<TimeEntry*>> day_entries;
        for (auto& entry : it->second->entries)
        {
            int day_number = entry->start.date().day().as_number();
            day_entries[day_number].emplace_back(entry.get());
        }

        // Check for overlaps in each day
        for (auto& day_entry : day_entries)
        {
            auto& entries = day_entry.second;
            // Sort entries by start time
            std::sort(entries.begin(), entries.end(),
                [](const TimeEntry* a, const TimeEntry* b) {
                    return a->start < b->start;
                });

            // Reset overlap flags first
            for (auto entry : entries) {
                entry->is_overlap = false;
            }

            // Check all pairs for overlaps (not just consecutive ones)
            for (size_t i = 0; i < entries.size(); ++i)
            {
                for (size_t j = i + 1; j < entries.size(); ++j)
                {
                    // If entry i ends before entry j starts, no overlap and no need to check further
                    if (entries[i]->end <= entries[j]->start)
                    {
                        break;
                    }

                    // Otherwise, there's some overlap
                    DBG("OVERLAP DETECTED on day %d between:\n", day_entry.first);
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

        for (auto& entry : it->second->entries)
        {
            int day_number = entry->start.date().day().as_number();

            auto duration = entry->end - entry->start;
            if(!entry->is_overlap && duration.is_positive())
                day_totals[day_number] += entry->end - entry->start;
        }
        
        bool is_first = true;
        bool is_insert_from_begining = true;
        bool is_white = true;
        boost::gregorian::date current_date; // Initially invalid
        for (auto& entry : it->second->entries)
        {
            bool is_set_date = false;
            boost::gregorian::date entry_date = entry->start.date();

            // Set date only if it's different from current date
            if (entry_date != current_date)
            {
                is_set_date = true;
                current_date = entry_date;

				is_white = !is_white; // Alternate color for each day
            }
            else
            {
                
            }

            is_first = false;

            std::unique_ptr<TimeEntrySerialized> serialized_entry =
                std::make_unique<TimeEntrySerialized>(entry->start.time_of_day(), entry->end.time_of_day(), entry->end - entry->start, entry.get());

            if (is_set_date)
            {
                serialized_entry->date = entry_date;
            }

			serialized_entry->is_white = is_white;
            if (serialized_entry->start > serialized_entry->end)
            {
                serialized_entry->is_time_bad = true;
            }

            const int day_number = entry_date.day();
            if (processed_days.find(day_number) == processed_days.end() && !entry->is_overlap)  // Only set total hours if we haven't processed this day yet
            {
                serialized_entry->total_duration_per_day = day_totals[day_number];
                it->second->total_worktime += serialized_entry->total_duration_per_day.total_seconds();
                processed_days.insert(day_number);

				//DBG("duration %d: %d:%d:%d\n", day_number, serialized_entry.total_duration_per_day.hours(), serialized_entry.total_duration_per_day.minutes(), serialized_entry.total_duration_per_day.seconds());
            }
            else
            {
                // Leave total_duration_per_day as default (blank) for other entries
                serialized_entry->total_duration_per_day = boost::posix_time::time_duration();
            }
            /*
			if (!serialized_entry.date.is_not_a_date())
			    DBG("date: %d: %d-%d-%d\n", day_number, serialized_entry.date.year(), serialized_entry.date.month().as_number(), serialized_entry.date.day());
			DBG("start %d: %d:%d:%d\n", day_number, serialized_entry.start.hours(), serialized_entry.start.minutes(), serialized_entry.start.seconds());
			DBG("end %d: %d:%d:%d\n", day_number, serialized_entry.end.hours(), serialized_entry.end.minutes(), serialized_entry.end.seconds());
			if (serialized_entry.total_duration_per_day != boost::posix_time::time_duration(0, 0, 0))
                DBG("total hours: %d: %d:%d:%d\n", day_number, serialized_entry.total_duration_per_day.hours(), serialized_entry.total_duration_per_day.minutes(), serialized_entry.total_duration_per_day.seconds());
			DBG("----\n\n");
            */
            it->second->serialized_entries[day_number].push_back(std::move(serialized_entry));
        }
    }

	// Step 3: Reorganize serialized entries
	//ReorganizeSerializedEntries(it->second->serialized_entries);
}

TimeEntry* TimeTracker::AddEntry(boost::posix_time::ptime start, boost::posix_time::ptime end, const std::string& comment, int sqlid)
{
    DBStream db_stream(db_name, m_db);
    if (!db_stream)
    {
        LOG(LogLevel::Critical, "Failed to open the database for measurements!");
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
    else
    {

    }

    std::unique_ptr<TimeEntry> entry = std::make_unique<TimeEntry>(start, end, comment, sqlid);

    int map_key = CalculateMapDateOffset(start);
	printf("Adding entry with map key: %d\n", map_key);
    auto [it, inserted] = m_Entries.try_emplace(map_key, std::make_unique<MonthlyTimeEntry>());

    it->second->entries.push_back(std::move(entry));
    auto ret = it->second->entries.back().get();
    return ret;
}

void TimeTracker::EditEntry(TimeEntry* entry, boost::posix_time::ptime start, boost::posix_time::ptime end, const std::string& comment)
{
	if (entry == nullptr)
		return;
	DBStream db_stream(db_name, m_db);
	if (!db_stream)
	{
		LOG(LogLevel::Critical, "Failed to open the database for measurements!");
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
	DBStream db_stream(db_name, m_db);
	if (!db_stream)
	{
		LOG(LogLevel::Critical, "Failed to open the database for measurements!");
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
	DBStream db_stream(db_name, m_db);
	if (!db_stream)
	{
		LOG(LogLevel::Critical, "Failed to open the database for measurements!");
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

int TimeTracker::CalculateMapDateOffset(boost::posix_time::ptime start)
{
    boost::gregorian::date date = start.date();  // Extract the date part
    int year = date.year();
    int month = date.month();

    return year * 100 + month;  // Format as YYYYMM
}

void TimeTracker::Query_OneMonth(std::unique_ptr<Result>& result, std::any param)  
{  
    int cols = result->GetColumnCount();  
    while (!m_destructing)  
    {  
        bool is_ok = result->StepNext();  
        if (!is_ok)  
            break;  

        int id = result->GetColumnInt(0);  
        int start = result->GetColumnInt(1);  
        int end = result->GetColumnInt(2);  
        const uint8_t* desc = result->GetColumnText(3);  

        std::string desc_str;  
        if (desc != NULL)  
            desc_str = std::string((const char*)desc);  
        AddEntry(boost::posix_time::from_time_t(start), boost::posix_time::from_time_t(end), desc_str, id);  
    }  

    // Corrected std::any_cast usage  
    auto yearmonth = std::any_cast<std::pair<int, int>>(param);  
    int year = yearmonth.first;  
    int month = yearmonth.second;  
    SerializeEntriesForOneMonth(year, month);  
}
