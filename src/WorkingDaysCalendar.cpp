#include "WorkingDaysCalendar.hpp"

#include <sstream>
#include <utility>
#include <vector>

namespace working_days
{
namespace
{
namespace greg = boost::gregorian;

// Fixed-date holidays and their names, shared by the description table so the
// two cannot drift apart the way two hand-written lists do.
struct FixedHoliday
{
    greg::months_of_year month;
    unsigned short day;
    const char* name;
};

constexpr FixedHoliday slovak_fixed[] = {
    {greg::Jan, 1, "New Year's Day"},
    {greg::Jan, 6, "Epiphany"},
    {greg::May, 1, "Labour Day"},
    {greg::May, 8, "Liberation Day"},
    {greg::Jul, 5, "St. Cyril and Methodius Day"},
    {greg::Aug, 29, "Slovak National Uprising Day"},
    {greg::Sep, 1, "Constitution Day"},
    {greg::Sep, 15, "Day of Our Lady of Sorrows"},
    {greg::Nov, 1, "All Saints' Day"},
    {greg::Nov, 17, "Struggle for Freedom and Democracy Day"},
    {greg::Dec, 24, "Christmas Eve"},
    {greg::Dec, 25, "Christmas Day"},
    {greg::Dec, 26, "St. Stephen's Day"},
};

constexpr FixedHoliday hungarian_fixed[] = {
    {greg::Jan, 1, "New Year's Day"},
    {greg::Mar, 15, "Revolution Day"},
    {greg::Aug, 20, "St. Stephen's Day"},
    {greg::Oct, 23, "1956 Revolution Memorial Day"},
    {greg::Dec, 25, "Christmas Day"},
    {greg::Dec, 26, "St. Stephen's Day"},
};

constexpr FixedHoliday austrian_fixed[] = {
    {greg::Jan, 1, "New Year's Day"},
    {greg::Jan, 6, "Epiphany"},
    {greg::May, 1, "Labour Day"},
    {greg::Aug, 15, "Assumption Day"},
    {greg::Oct, 26, "National Day"},
    {greg::Nov, 1, "All Saints' Day"},
    {greg::Dec, 8, "Immaculate Conception"},
    {greg::Dec, 25, "Christmas Day"},
    {greg::Dec, 26, "St. Stephen's Day"},
};

// Every fixed holiday any of the three countries observes, for naming. A date
// is named by the first entry that matches it, which is how the original table
// behaved for the days more than one country shares.
constexpr const FixedHoliday* all_tables[] = {slovak_fixed, hungarian_fixed, austrian_fixed};
constexpr std::size_t all_sizes[] = {std::size(slovak_fixed), std::size(hungarian_fixed),
                                     std::size(austrian_fixed)};

template <std::size_t N>
[[nodiscard]] DateSet WithEaster(int year, const FixedHoliday (&fixed)[N],
                                 const std::vector<int>& easter_offsets)
{
    DateSet holidays;
    for(const auto& entry : fixed)
        holidays.insert(Date(static_cast<greg::greg_year::value_type>(year), entry.month, entry.day));

    const auto easter = Easter(year);
    for(const int offset : easter_offsets)
        holidays.insert(easter + greg::days(offset));
    return holidays;
}
}

Date Easter(int year)
{
    /* Anonymous Gregorian computus. Kept exactly as it was, now with dates to
       check it against. */
    const int a = year % 19;
    const int b = year / 100;
    const int c = year % 100;
    const int d = b / 4;
    const int e = b % 4;
    const int f = (b + 8) / 25;
    const int g = (b - f + 1) / 3;
    const int h = (19 * a + b - d - g + 15) % 30;
    const int i = c / 4;
    const int k = c % 4;
    const int l = (32 + 2 * e + 2 * i - h - k) % 7;
    const int m = (a + 11 * h + 22 * l) / 451;
    const int month = (h + l - 7 * m + 114) / 31;
    const int day = ((h + l - 7 * m + 114) % 31) + 1;

    return Date(static_cast<greg::greg_year::value_type>(year),
                static_cast<greg::greg_month::value_type>(month),
                static_cast<greg::greg_day::value_type>(day));
}

bool IsWeekend(const Date& day)
{
    return day.day_of_week() == greg::Saturday || day.day_of_week() == greg::Sunday;
}

DateSet SlovakHolidays(int year)
{
    return WithEaster(year, slovak_fixed, {1});  /* Easter Monday */
}

DateSet HungarianHolidays(int year)
{
    return WithEaster(year, hungarian_fixed, {1, 50});  /* Easter Monday, Pentecost Monday */
}

DateSet AustrianHolidays(int year)
{
    /* Easter Monday, Ascension Day, Pentecost Monday, Corpus Christi */
    return WithEaster(year, austrian_fixed, {1, 39, 50, 60});
}

std::map<std::string, std::string> Descriptions(const DateSet& holidays)
{
    std::map<std::string, std::string> descriptions;
    for(const auto& holiday : holidays)
    {
        const char* name = "Unknown Holiday";  /* the moveable feasts land here */
        for(std::size_t table = 0; table < std::size(all_tables) && name[0] == 'U'; ++table)
        {
            for(std::size_t entry = 0; entry < all_sizes[table]; ++entry)
            {
                const auto& fixed = all_tables[table][entry];
                if(holiday.month() == fixed.month && holiday.day() == fixed.day)
                {
                    name = fixed.name;
                    break;
                }
            }
        }
        descriptions[greg::to_simple_string(holiday)] = name;
    }
    return descriptions;
}

Count CountBetween(const Date& start, const Date& end, const DateSet& holidays, int described_month)
{
    Count count;
    std::ostringstream holidays_stream;
    const auto descriptions = Descriptions(holidays);

    for(greg::day_iterator day = start; day <= end; ++day)
    {
        const bool is_holiday = holidays.find(*day) != holidays.end();
        if(!IsWeekend(*day) && !is_holiday)
            ++count.working_days;

        if(!is_holiday)
            continue;

        ++count.holidays;
        if(static_cast<int>(day->month()) != described_month)
            continue;

        const auto named = descriptions.find(greg::to_simple_string(*day));
        holidays_stream << day->day() << ". "
                        << (named == descriptions.end() ? std::string() : named->second) << "\n";
    }

    count.holidays_text = holidays_stream.str();
    return count;
}
}
