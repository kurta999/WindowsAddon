#pragma once

#include <boost/date_time/gregorian/gregorian.hpp>

#include <map>
#include <set>
#include <string>

// The date arithmetic behind the working-day counters, kept apart from the
// singleton that holds the results so it can be tested: every answer here
// depends only on its arguments, where the counting used to read the wall clock
// halfway through.
namespace working_days
{
using Date = boost::gregorian::date;
using DateSet = std::set<Date>;

// !\brief Easter Sunday, by the anonymous Gregorian computus.
[[nodiscard]] Date Easter(int year);

[[nodiscard]] bool IsWeekend(const Date& day);

[[nodiscard]] DateSet SlovakHolidays(int year);
[[nodiscard]] DateSet HungarianHolidays(int year);
[[nodiscard]] DateSet AustrianHolidays(int year);

// !\brief Names for the holidays, keyed by their simple-string date.
[[nodiscard]] std::map<std::string, std::string> Descriptions(const DateSet& holidays);

struct Count
{
    int working_days = 0;
    int holidays = 0;
    // One line per holiday falling in `described_month`.
    std::string holidays_text;
};

// !\brief Count over [start, end]. `described_month` selects which holidays are
// listed in the text; the counts cover the whole range either way.
[[nodiscard]] Count CountBetween(const Date& start, const Date& end,
                                 const DateSet& holidays, int described_month);
}
