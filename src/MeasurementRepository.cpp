#include "pch.hpp"

MeasurementRepository::MeasurementRepository(const char* db_name, IDatabase& db)
    : m_db_name(db_name), m_db(db)
{}

void MeasurementRepository::Insert(const Measurement& m)
{
    DBStream stream(m_db_name, m_db);
    if(!stream)
    {
        LOG(LogLevel::Critical, "Failed to open the database for measurements!");
        return;
    }

    stream.ExecuteQuery(
        "CREATE TABLE IF NOT EXISTS data("
        "sensor_id INT NOT NULL,"
        "temp      INT NOT NULL,"
        "hum       INT NOT NULL,"
        "co2       INT NOT NULL,"
        "voc       INT NOT NULL,"
        "co        INT NOT NULL,"
        "pm25      INT NOT NULL,"
        "pm10      INT NOT NULL,"
        "pressure  INT NOT NULL,"
        "r         INT NOT NULL,"
        "g         INT NOT NULL,"
        "b         INT NOT NULL,"
        "lux       INT NOT NULL,"
        "cct       INT NOT NULL,"
        "uv        INT NOT NULL,"
        "time      INT NOT NULL)");

    stream.ExecuteQuery(std::format(
        "INSERT INTO data(sensor_id, temp, hum, co2, voc, co, pm25, pm10, pressure, r, g, b, lux, cct, uv, time) "
        "VALUES(1, {}, {}, {}, {}, {}, {}, {}, {}, {}, {}, {}, {}, {}, {}, strftime('%s', 'now', 'localtime'))",
        static_cast<int>(std::round(m.temp     * 10.f)),
        static_cast<int>(std::round(m.hum      * 10.f)),
        m.co2,
        static_cast<int>(std::round(m.voc      * 10.f)),
        m.co, m.pm25, m.pm10,
        static_cast<int>(std::round(m.pressure * 10.f)),
        static_cast<int>(std::round(m.r        * 10.f)),
        static_cast<int>(std::round(m.g        * 10.f)),
        static_cast<int>(std::round(m.b        * 10.f)),
        m.lux, m.cct, m.uv));
}
