#include "pch.hpp"

void WorkingDays::Update()
{
    boost::gregorian::date today = boost::gregorian::day_clock::local_day();
    int year = today.year();
    int month = today.month();
    try
    {
        boost::gregorian::date first_day_of_month(year, month, 1);
        boost::gregorian::date last_day_of_month = first_day_of_month.end_of_month();

        // Slovakia
        std::set<boost::gregorian::date> slovak_holidays = GetSlovakHolidays(year);
        CountWorkingDaysForCountry(first_day_of_month, last_day_of_month, slovak_holidays, m_WorkingDaysSlovakia, m_HolidaysSlovakia, m_HolidaysStrSlovakia);

        // Hungary
        std::set<boost::gregorian::date> hungarian_holidays = GetHungarianHolidays(year);
        CountWorkingDaysForCountry(first_day_of_month, last_day_of_month, hungarian_holidays, m_WorkingDaysHungary, m_HolidaysHungary, m_HolidaysStrHungary);

        // Austria
        std::set<boost::gregorian::date> austrian_holidays = GetAustrianHolidays(year);
        CountWorkingDaysForCountry(first_day_of_month, last_day_of_month, austrian_holidays, m_WorkingDaysAustria, m_HolidaysAustria, m_HolidaysStrAustria);
    }
    catch (std::exception& e)
    {
        LOG(LogLevel::Error, "Exception with working days: {}", e.what());
    }
}

bool WorkingDays::IsWeekend(boost::gregorian::day_iterator dit)
{
    return dit->day_of_week() == boost::gregorian::Saturday || dit->day_of_week() == boost::gregorian::Sunday;
}

boost::gregorian::date WorkingDays::CalculateEaster(int year)
{
    int a = year % 19;
    int b = year / 100;
    int c = year % 100;
    int d = b / 4;
    int e = b % 4;
    int f = (b + 8) / 25;
    int g = (b - f + 1) / 3;
    int h = (19 * a + b - d - g + 15) % 30;
    int i = c / 4;
    int k = c % 4;
    int l = (32 + 2 * e + 2 * i - h - k) % 7;
    int m = (a + 11 * h + 22 * l) / 451;
    int month = (h + l - 7 * m + 114) / 31;
    int day = ((h + l - 7 * m + 114) % 31) + 1;

    return boost::gregorian::date(year, month, day);
}

std::set<boost::gregorian::date> WorkingDays::GetSlovakHolidays(int year)
{
    std::set<boost::gregorian::date> holidays =
    {
        boost::gregorian::date(year, boost::gregorian::Jan, 1),   // New Year's Day
        boost::gregorian::date(year, boost::gregorian::Jan, 6),   // Epiphany
        boost::gregorian::date(year, boost::gregorian::May, 1),   // Labour Day
        boost::gregorian::date(year, boost::gregorian::May, 8),   // Liberation Day
        boost::gregorian::date(year, boost::gregorian::Jul, 5),   // St. Cyril and Methodius Day
        boost::gregorian::date(year, boost::gregorian::Aug, 29),  // Slovak National Uprising Day
        boost::gregorian::date(year, boost::gregorian::Sep, 1),   // Constitution Day
        boost::gregorian::date(year, boost::gregorian::Sep, 15),  // Day of Our Lady of Sorrows
        boost::gregorian::date(year, boost::gregorian::Nov, 1),   // All Saints' Day
        boost::gregorian::date(year, boost::gregorian::Nov, 17),  // Struggle for Freedom and Democracy Day
        boost::gregorian::date(year, boost::gregorian::Dec, 24),  // Christmas Eve
        boost::gregorian::date(year, boost::gregorian::Dec, 25),  // Christmas Day
        boost::gregorian::date(year, boost::gregorian::Dec, 26)   // St. Stephen's Day
    };

    boost::gregorian::date easter_sunday = CalculateEaster(year);
    holidays.insert(easter_sunday + boost::gregorian::days(1));  // Easter Monday

    return holidays;
}

std::set<boost::gregorian::date> WorkingDays::GetHungarianHolidays(int year)
{
    std::set<boost::gregorian::date> holidays =
    {
        boost::gregorian::date(year, boost::gregorian::Jan, 1),   // New Year's Day
        boost::gregorian::date(year, boost::gregorian::Mar, 15),  // Revolution Day
        boost::gregorian::date(year, boost::gregorian::Aug, 20),  // St. Stephen's Day
        boost::gregorian::date(year, boost::gregorian::Oct, 23),  // 1956 Revolution Memorial Day
        boost::gregorian::date(year, boost::gregorian::Dec, 25),  // Christmas Day
        boost::gregorian::date(year, boost::gregorian::Dec, 26)   // St. Stephen's Day
    };

    boost::gregorian::date easter_sunday = CalculateEaster(year);
    holidays.insert(easter_sunday + boost::gregorian::days(1));  // Easter Monday
    holidays.insert(easter_sunday + boost::gregorian::days(50)); // Pentecost Monday

    return holidays;
}

std::set<boost::gregorian::date> WorkingDays::GetAustrianHolidays(int year)
{
    std::set<boost::gregorian::date> holidays =
    {
        boost::gregorian::date(year, boost::gregorian::Jan, 1),   // New Year's Day
        boost::gregorian::date(year, boost::gregorian::Jan, 6),   // Epiphany
        boost::gregorian::date(year, boost::gregorian::May, 1),   // Labour Day
        boost::gregorian::date(year, boost::gregorian::Aug, 15),  // Assumption Day
        boost::gregorian::date(year, boost::gregorian::Oct, 26),  // National Day
        boost::gregorian::date(year, boost::gregorian::Nov, 1),   // All Saints' Day
        boost::gregorian::date(year, boost::gregorian::Dec, 8),   // Immaculate Conception
        boost::gregorian::date(year, boost::gregorian::Dec, 25),  // Christmas Day
        boost::gregorian::date(year, boost::gregorian::Dec, 26)   // St. Stephen's Day
    };

    boost::gregorian::date easter_sunday = CalculateEaster(year);
    holidays.insert(easter_sunday + boost::gregorian::days(1));  // Easter Monday
    holidays.insert(easter_sunday + boost::gregorian::days(39)); // Ascension Day
    holidays.insert(easter_sunday + boost::gregorian::days(50)); // Pentecost Monday
    holidays.insert(easter_sunday + boost::gregorian::days(60)); // Corpus Christi

    return holidays;
}

std::map<std::string, std::string> WorkingDays::GetHolidayDescriptions(const std::set<boost::gregorian::date>& holidays)
{
    std::map<std::string, std::string> descriptions;
    for (const auto& holiday : holidays)
    {
        if (holiday.month() == boost::gregorian::Jan && holiday.day() == 1)
            descriptions[boost::gregorian::to_simple_string(holiday)] = "New Year's Day";
        else if (holiday.month() == boost::gregorian::Jan && holiday.day() == 6)
            descriptions[boost::gregorian::to_simple_string(holiday)] = "Epiphany";
        else if (holiday.month() == boost::gregorian::May && holiday.day() == 1)
            descriptions[boost::gregorian::to_simple_string(holiday)] = "Labour Day";
        else if (holiday.month() == boost::gregorian::May && holiday.day() == 8)
            descriptions[boost::gregorian::to_simple_string(holiday)] = "Liberation Day";
        else if (holiday.month() == boost::gregorian::Jul && holiday.day() == 5)
            descriptions[boost::gregorian::to_simple_string(holiday)] = "St. Cyril and Methodius Day";
        else if (holiday.month() == boost::gregorian::Aug && holiday.day() == 29)
            descriptions[boost::gregorian::to_simple_string(holiday)] = "Slovak National Uprising Day";
        else if (holiday.month() == boost::gregorian::Sep && holiday.day() == 1)
            descriptions[boost::gregorian::to_simple_string(holiday)] = "Constitution Day";
        else if (holiday.month() == boost::gregorian::Sep && holiday.day() == 15)
            descriptions[boost::gregorian::to_simple_string(holiday)] = "Day of Our Lady of Sorrows";
        else if (holiday.month() == boost::gregorian::Nov && holiday.day() == 1)
            descriptions[boost::gregorian::to_simple_string(holiday)] = "All Saints' Day";
        else if (holiday.month() == boost::gregorian::Nov && holiday.day() == 17)
            descriptions[boost::gregorian::to_simple_string(holiday)] = "Struggle for Freedom and Democracy Day";
        else if (holiday.month() == boost::gregorian::Dec && holiday.day() == 24)
            descriptions[boost::gregorian::to_simple_string(holiday)] = "Christmas Eve";
        else if (holiday.month() == boost::gregorian::Dec && holiday.day() == 25)
            descriptions[boost::gregorian::to_simple_string(holiday)] = "Christmas Day";
        else if (holiday.month() == boost::gregorian::Dec && holiday.day() == 26)
            descriptions[boost::gregorian::to_simple_string(holiday)] = "St. Stephen's Day";
        else
            descriptions[boost::gregorian::to_simple_string(holiday)] = "Unknown Holiday";
    }
    return descriptions;
}

bool WorkingDays::IsSlovakHoliday(const boost::gregorian::date& d, const std::set<boost::gregorian::date>& holidays)
{
    return holidays.find(d) != holidays.end();
}

void WorkingDays::CountWorkingDaysForCountry(const boost::gregorian::date& start_date, const boost::gregorian::date& end_date, const std::set<boost::gregorian::date>& holidays, int& working_days, int& holidays_count, std::string& holidays_str)
{
    working_days = 0;
    holidays_count = 0;
    std::ostringstream holidays_stream;
    boost::gregorian::date today = boost::gregorian::day_clock::local_day();
    int current_month = today.month();
    auto holiday_descriptions = GetHolidayDescriptions(holidays);

    for (boost::gregorian::day_iterator dit = start_date; dit <= end_date; ++dit)
    {
        if (!IsWeekend(dit) && holidays.find(*dit) == holidays.end())
        {
            ++working_days;
        }
        if (holidays.find(*dit) != holidays.end())
        {
            ++holidays_count;
            if (dit->month() == current_month)
            {
                auto holiday_name = holiday_descriptions[boost::gregorian::to_simple_string(*dit)];
                holidays_stream << dit->day() << ". " << holiday_name << "\n";
            }
        }
    }

    holidays_str = holidays_stream.str();
}