#include "pch_core.hpp"
#include "WorkingDays.hpp"
#include "WorkingDaysCalendar.hpp"
#include "Logger.hpp"
#include "Utils.hpp"

void WorkingDays::Update()
{
    const boost::gregorian::date today = boost::gregorian::day_clock::local_day();
    const int year = today.year();
    const int month = today.month();
    try
    {
        const boost::gregorian::date first_day_of_month(
            static_cast<boost::gregorian::greg_year::value_type>(year),
            static_cast<boost::gregorian::greg_month::value_type>(month), 1);
        const boost::gregorian::date last_day_of_month = first_day_of_month.end_of_month();

        const auto apply = [&](const working_days::DateSet& holidays, int& working, int& count,
                               std::string& text) {
            const auto result = working_days::CountBetween(first_day_of_month, last_day_of_month,
                holidays, month);
            working = result.working_days;
            count = result.holidays;
            text = result.holidays_text;
        };

        apply(working_days::SlovakHolidays(year), m_WorkingDaysSlovakia, m_HolidaysSlovakia,
            m_HolidaysStrSlovakia);
        apply(working_days::HungarianHolidays(year), m_WorkingDaysHungary, m_HolidaysHungary,
            m_HolidaysStrHungary);
        apply(working_days::AustrianHolidays(year), m_WorkingDaysAustria, m_HolidaysAustria,
            m_HolidaysStrAustria);
    }
    catch(std::exception& e)
    {
        LOG(LogLevel::Error, "Exception with working days: {}", e.what());
    }
}
