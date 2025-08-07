#include "utils/CSingleton.hpp"

#include <boost/date_time/gregorian/gregorian.hpp>

class WorkingDays : public CSingleton < WorkingDays >
{
    friend class CSingleton < WorkingDays >;

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

private:
    bool IsWeekend(boost::gregorian::day_iterator dit);
    boost::gregorian::date CalculateEaster(int year);
    std::set<boost::gregorian::date> GetSlovakHolidays(int year);
    std::set<boost::gregorian::date> GetHungarianHolidays(int year);
    std::set<boost::gregorian::date> GetAustrianHolidays(int year);
    std::map<std::string, std::string> GetHolidayDescriptions(const std::set<boost::gregorian::date>& holidays);
    bool IsSlovakHoliday(const boost::gregorian::date& d, const std::set<boost::gregorian::date>& holidays);
    void CountWorkingDaysForCountry(const boost::gregorian::date& start_date, const boost::gregorian::date& end_date, const std::set<boost::gregorian::date>& holidays, int& working_days, int& holidays_count, std::string& holidays_str);
};