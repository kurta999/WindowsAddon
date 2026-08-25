#include "TimeTracker.hpp"

#include <algorithm>
#include <set>
#include <utility>

#include <boost/date_time/gregorian/gregorian.hpp>
#include <boost/date_time/posix_time/posix_time.hpp>
#include "SettingsReader.hpp"
#include "SettingsWriter.hpp"
#include <charconv>
#include <ostream>
#include <stdexcept>

namespace {
    constexpr int64_t      SecondsPerDay = 86400;

    int64_t secondsSinceEpoch(const boost::posix_time::ptime& time)
    {
        namespace bpt = boost::posix_time;
        const bpt::ptime epoch = bpt::from_time_t(0);
        return (time - epoch).total_seconds();
    }
}

TimeTracker::TimeTracker(std::unique_ptr<ITimeTrackerStorage> storage, ErrorHandler error_handler) :
    m_storage(std::move(storage)),
    m_error_handler(std::move(error_handler))
{}

void TimeTracker::Init()
{
    if(!m_storage || !m_storage->IsOpen())
    {
        const std::string error = m_storage ? m_storage->LastError() : "storage dependency is missing";
        ReportError("Failed to open the time tracker database: " + error);
        return;
    }

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
    if(year < 1400 || year > 9999 || month < 1 || month > 12)
    {
        ReportError("Failed to load time entries: invalid year or month");
        return;
    }
    boost::gregorian::date first_day(
        static_cast<unsigned short>(year), static_cast<unsigned short>(month), 1);
    boost::gregorian::date last_day = first_day.end_of_month();

    boost::posix_time::ptime epoch(boost::gregorian::date(1970, 1, 1));
    int64_t first_timestamp = (first_day - epoch.date()).days() * SecondsPerDay;
    int64_t last_timestamp  = (last_day  - epoch.date()).days() * SecondsPerDay + (SecondsPerDay - 1);

    if(!m_storage || !m_storage->IsOpen())
    {
        ReportError("Failed to load time entries: storage is not open");
        return;
    }
    for(const auto& row : m_storage->LoadRange(first_timestamp, last_timestamp))
    {
        AddEntry(epoch + boost::posix_time::seconds(row.start),
                 epoch + boost::posix_time::seconds(row.end), row.comment, row.id);
    }
    SerializeEntriesForOneMonth(year, month);
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
    if(year < 1400 || year > 9999 || month < 1 || month > 12)
    {
        ReportError("Failed to serialize time entries: invalid year or month");
        return;
    }
    boost::gregorian::date date(
        static_cast<unsigned short>(year), static_cast<unsigned short>(month), 1);
    boost::posix_time::ptime month_start(date);
    int offset = CalculateMapDateOffset(month_start);
    auto it = m_Entries.find(offset);
    if (it == m_Entries.end())
        return;

    it->second->serialized_entries.clear();
    it->second->total_worktime = 0;

    // First pass: group entries by day and compute overlap flags
    std::map<int, boost::posix_time::time_duration> day_totals;
    std::vector<time_tracker_logic::Interval> intervals;
    intervals.reserve(it->second->entries.size());
    for (auto& entry : it->second->entries)
    {
        intervals.push_back({secondsSinceEpoch(entry->start), secondsSinceEpoch(entry->end)});
    }

    const auto overlaps = time_tracker_logic::DetectOverlaps(intervals);
    for(size_t index = 0; index < it->second->entries.size(); ++index)
    {
        it->second->entries[index]->is_overlap = overlaps[index];
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
    if(!m_storage || !m_storage->IsOpen())
    {
        ReportError("Failed to add time entry: storage is not open");
        return nullptr;
    }

    if (sqlid == 0)
    {
        const auto inserted_id = m_storage->Insert(secondsSinceEpoch(start), secondsSinceEpoch(end), comment);
        if(!inserted_id)
        {
            ReportError("Failed to insert time entry: " + m_storage->LastError());
            return nullptr;
        }
        m_LastId = *inserted_id;
        sqlid = m_LastId;
    }

    std::unique_ptr<TimeEntry> entry = std::make_unique<TimeEntry>(start, end, comment, sqlid);

    int map_key = CalculateMapDateOffset(start);
    auto [it, inserted] = m_Entries.try_emplace(map_key, std::make_unique<MonthlyTimeEntry>());

    it->second->entries.push_back(std::move(entry));
    auto ret = it->second->entries.back().get();
    return ret;
}

TimeEntry* TimeTracker::FindEntry(int sql_id)
{
    for(auto& [map_key, month] : m_Entries)
    {
        (void)map_key;
        for(auto& entry : month->entries)
        {
            if(entry->sql_id == sql_id)
                return entry.get();
        }
    }
    return nullptr;
}

const TimeEntry* TimeTracker::FindEntry(int sql_id) const
{
    for(const auto& [map_key, month] : m_Entries)
    {
        (void)map_key;
        for(const auto& entry : month->entries)
        {
            if(entry->sql_id == sql_id)
                return entry.get();
        }
    }
    return nullptr;
}

bool TimeTracker::EditEntry(int sql_id, boost::posix_time::ptime start, boost::posix_time::ptime end, const std::string& comment)
{
    TimeEntry* entry = FindEntry(sql_id);
    if(!entry)
    {
        ReportError("Failed to edit time entry: entry is no longer available");
        return false;
    }
    if(!m_storage || !m_storage->IsOpen())
    {
        ReportError("Failed to edit time entry: storage is not open");
        return false;
    }
    if(!m_storage->Update(sql_id, secondsSinceEpoch(start), secondsSinceEpoch(end), comment))
    {
        ReportError("Failed to edit time entry: " + m_storage->LastError());
        return false;
    }

    entry->start = start;
    entry->end = end;
    entry->desc = comment;
    if(!MoveEntryToMonth(sql_id, CalculateMapDateOffset(start)))
    {
        ReportError("Failed to edit time entry: entry ownership was lost");
        return false;
    }
    return true;
}

bool TimeTracker::SaveEntry(int sql_id)
{
    const TimeEntry* entry = FindEntry(sql_id);
    if(!entry)
    {
        ReportError("Failed to save time entry: entry is no longer available");
        return false;
    }
    if(!m_storage || !m_storage->IsOpen())
    {
        ReportError("Failed to save time entry: storage is not open");
        return false;
    }
    if(!m_storage->Update(sql_id, secondsSinceEpoch(entry->start), secondsSinceEpoch(entry->end), entry->desc))
    {
        ReportError("Failed to save time entry: " + m_storage->LastError());
        return false;
    }
    return true;
}

bool TimeTracker::RemoveEntry(int sql_id)
{
    if(!FindEntry(sql_id))
    {
        ReportError("Failed to remove time entry: entry is no longer available");
        return false;
    }
    if(!m_storage || !m_storage->IsOpen())
    {
        ReportError("Failed to remove time entry: storage is not open");
        return false;
    }
    if(!m_storage->Remove(sql_id))
    {
        ReportError("Failed to remove time entry: " + m_storage->LastError());
        return false;
    }

    for(auto month = m_Entries.begin(); month != m_Entries.end(); ++month)
    {
        auto& entries = month->second->entries;
        const auto entry = std::ranges::find(entries, sql_id, &TimeEntry::sql_id);
        if(entry == entries.end())
            continue;

        entries.erase(entry);
        if(entries.empty())
            m_Entries.erase(month);
        return true;
    }

    ReportError("Failed to remove time entry: entry ownership was lost");
    return false;
}

bool TimeTracker::MoveEntryToMonth(int sql_id, int new_map_key)
{
    for(auto month = m_Entries.begin(); month != m_Entries.end(); ++month)
    {
        auto& entries = month->second->entries;
        const auto entry = std::ranges::find(entries, sql_id, &TimeEntry::sql_id);
        if(entry == entries.end())
            continue;
        if(month->first == new_map_key)
            return true;

        auto moved_entry = std::move(*entry);
        entries.erase(entry);
        const bool remove_old_month = entries.empty();

        auto [destination, inserted] = m_Entries.try_emplace(
            new_map_key, std::make_unique<MonthlyTimeEntry>());
        (void)inserted;
        destination->second->entries.push_back(std::move(moved_entry));
        if(remove_old_month)
            m_Entries.erase(month);
        return true;
    }
    return false;
}

void TimeTracker::ReportError(std::string message)
{
    m_last_error = std::move(message);
    if(m_error_handler)
        m_error_handler(m_last_error);
}

int TimeTracker::CalculateMapDateOffset(const boost::posix_time::ptime& start)
{
    const boost::gregorian::date date = start.date();
    return date.year() * 100 + date.month();
}

namespace
{
/* Persistence deliberately does not depend on the application's utils layer, so
   the whole-string integer parse it would otherwise borrow lives here. */
[[nodiscard]] int ParseWholeInt(const std::string& text)
{
    int value = 0;
    const auto result = std::from_chars(text.data(), text.data() + text.size(), value);
    if(result.ec != std::errc() || result.ptr != text.data() + text.size())
        throw std::runtime_error("Bad integer input (" + text + ")");
    return value;
}
}

void TimeTracker::LoadSettings(SettingsReader& reader)
{
    SetHourlyRate(ParseWholeInt(reader.Required("TimeTracker", "HourlyRate")));
    SetToggleKey(reader.Required("TimeTracker", "WorktimeCounterKey"));
}

void TimeTracker::SaveSettings(std::ostream& out) const
{
    /* No trailing blank line - see the note in DatabaseLogic::SaveSettings. */
    SettingsWriter(out, "TimeTracker")
        .Key("HourlyRate", GetHourlyRate())
        .Key("WorktimeCounterKey", GetToggleKey());
}
