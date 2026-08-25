#pragma once

#include <boost/date_time/gregorian/gregorian.hpp>

#include <string>

// !\brief How many working days and public holidays this month has, per country.
//
// A value holder with one method that fills it in. It was a singleton, which
// meant the one panel that shows the numbers reached for it eleven times
// rather than being handed it once.
class WorkingDays
{
public:
    WorkingDays() = default;
    ~WorkingDays() = default;

    // !\brief Initialize TCP backend server for sensors
    void Update();

    int m_WorkingDaysSlovakia;
    int m_HolidaysSlovakia;
    std::string m_HolidaysStrSlovakia;

    int m_WorkingDaysHungary;
    int m_HolidaysHungary;
    std::string m_HolidaysStrHungary;

    int m_WorkingDaysAustria;
    int m_HolidaysAustria;
    std::string m_HolidaysStrAustria;

    /* The date arithmetic lives in working_days (WorkingDaysCalendar.hpp),
       where it can be tested without waiting for the right day of the year. */
};