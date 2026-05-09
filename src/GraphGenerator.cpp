#include "pch.hpp"

GraphGenerator::GraphGenerator(const char* db_name, IDatabase& db)
    : m_db_name(db_name), m_db(db)
{}

GraphGenerator::~GraphGenerator()
{
    Shutdown();
}

void GraphGenerator::Shutdown()
{
    m_destructing = true;
    WaitIfRunning();
}

void GraphGenerator::WaitIfRunning()
{
    if(m_future.valid())
        m_future.get();
}

void GraphGenerator::Generate()
{
    if(m_future.valid())
        if(m_future.wait_for(std::chrono::nanoseconds(1)) != std::future_status::ready)
            return;
    m_future = std::async(std::launch::async, &GraphGenerator::DoGenerate, this);
}

void GraphGenerator::SetGraphHours(uint8_t slot, uint32_t hours)
{
    if(slot == 0) m_hours_1 = hours;
    else          m_hours_2 = hours;
}

uint32_t GraphGenerator::GetGraphHours(uint8_t slot) const
{
    return slot == 0 ? m_hours_1 : m_hours_2;
}

// ---------------------------------------------------------------------------
// Static helper: maps a result row to a Measurement.
// base = column offset: 2 for the "latest" query (has rowid+sensor_id prefix),
//                       0 for the avg/max/min-from-past queries.
// ---------------------------------------------------------------------------
Measurement GraphGenerator::ParseRow(Result& result, int base)
{
    auto getFloat = [&](int rel) -> float {
        const auto sv = result.GetColumnText(base + rel);
        return sv.empty() ? 0.0f : boost::lexical_cast<float>(sv) / 10.0f;
    };
    auto getInt = [&](int rel) {
        return result.GetColumnInt(base + rel);
    };

    return Measurement(
        getFloat(0),                                     // temp
        getFloat(1),                                     // hum
        getInt(2),                                       // co2
        getFloat(3),                                     // voc
        getInt(4),                                       // co
        getInt(5),                                       // pm25
        getInt(6),                                       // pm10
        getFloat(7),                                     // pressure
        getFloat(8),                                     // r
        getFloat(9),                                     // g
        getFloat(10),                                    // b
        getInt(11),                                      // lux
        getInt(12),                                      // cct
        getInt(13),                                      // uv
        std::string{ result.GetColumnText(base + 14) }  // time
    );
}

void GraphGenerator::QueryLatest(std::unique_ptr<Result>& result)
{
    // Latest query columns: rowid(0), sensor_id(1), then measurement data starting at col 2.
    while(!m_destructing && result->StepNext())
        Sensors::Get()->AddMeasurement(std::make_unique<Measurement>(ParseRow(*result, 2)));
}

void GraphGenerator::QueryMeasFromPast(std::unique_ptr<Result>& result, std::vector<std::unique_ptr<Measurement>>* out)
{
    while(!m_destructing && result->StepNext())
        out->push_back(std::make_unique<Measurement>(ParseRow(*result, 0)));
}

void GraphGenerator::DoGenerate()
{
    const auto t_start = std::chrono::steady_clock::now();

    DBStream stream(m_db_name, m_db);
    if(!stream)
    {
        LOG(LogLevel::Critical, "Failed to open the database for graph generation!");
        return;
    }

    Sensors* sensors = Sensors::Get();
    const uint16_t resolution = sensors->GetGraphResolution();

    for(auto& v : sensors->last_day)  v.clear();
    for(auto& v : sensors->last_week) v.clear();

    stream.SendQueryAndFetch(
        std::format("SELECT * FROM(SELECT rowid, sensor_id, temp, hum, co2, voc, co, pm25, pm10, "
                    "pressure, r, g, b, lux, cct, uv, strftime('%H:%M:%S', time, 'unixepoch') as date_time "
                    "FROM data ORDER BY rowid DESC LIMIT {}) ORDER BY rowid ASC", resolution),
        [this](std::unique_ptr<Result>& r) { QueryLatest(r); });

    // avg/max/min for the day window (3 passes into last_day[0..2])
    const std::string_view agg_cols[] = { "AVG", "MAX", "MIN" };
    for(int i = 0; i < 3; ++i)
    {
        const auto& agg = agg_cols[i];
        auto* day_dest  = &sensors->last_day[i];
        auto* week_dest = &sensors->last_week[i];

        stream.SendQueryAndFetch(
            std::format("SELECT {0}(temp), {0}(hum), {0}(co2), {0}(voc), {0}(co), {0}(pm25), {0}(pm10), "
                        "{0}(pressure), {0}(r), {0}(g), {0}(b), {0}(lux), {0}(cct), {0}(uv), "
                        "strftime('%H:%M:%S', time, 'unixepoch') "
                        "FROM (SELECT *, NTILE({1}) OVER (ORDER BY time) grp FROM data "
                        "WHERE time > (strftime('%s', 'now') - {2})) GROUP BY grp",
                        agg, resolution, m_hours_1 * 3600),
            [this, day_dest](std::unique_ptr<Result>& r) { QueryMeasFromPast(r, day_dest); });

        stream.SendQueryAndFetch(
            std::format("SELECT {0}(temp), {0}(hum), {0}(co2), {0}(voc), {0}(co), {0}(pm25), {0}(pm10), "
                        "{0}(pressure), {0}(r), {0}(g), {0}(b), {0}(lux), {0}(cct), {0}(uv), "
                        "strftime('%Y-%m-%d %H:%M:%S', time, 'unixepoch') "
                        "FROM (SELECT *, NTILE({1}) OVER (ORDER BY time) grp FROM data "
                        "WHERE time > (strftime('%s', 'now') - {2})) GROUP BY grp",
                        agg, resolution, m_hours_2 * 3600),
            [this, week_dest](std::unique_ptr<Result>& r) { QueryMeasFromPast(r, week_dest); });
    }

    sensors->WriteGraphs();

    const auto elapsed_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::steady_clock::now() - t_start).count();
    LOG(LogLevel::Notification, "Graph generation (6 queries) took {:.3f} ms",
        static_cast<double>(elapsed_ns) / 1'000'000.0);

    last_update = std::chrono::steady_clock::now();
}
