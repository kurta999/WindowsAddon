#include "pch.hpp"
#include <regex>

// ---------------------------------------------------------------------------
// Init
// ---------------------------------------------------------------------------

void Sensors::Init()
{
    std::error_code ec;
    if(!std::filesystem::exists("Graphs"))
        std::filesystem::create_directory("Graphs", ec);
    if(ec)
        LOG(LogLevel::Error, "Error with create_directory (Graphs): {}", ec.message());

    std::ifstream t("Graphs/template.html", std::ifstream::binary);
    if(!t)
    {
        LOG(LogLevel::Error, "Missing template.html from 'Graphs' folder, disabling sensor module.");
        return;
    }
    t.seekg(0, std::ios::end);
    m_templateStr.resize(static_cast<size_t>(t.tellg()));
    t.seekg(0);
    t.read(m_templateStr.data(), static_cast<std::streamsize>(m_templateStr.size()));
}

// ---------------------------------------------------------------------------
// Incoming data entry point
// ---------------------------------------------------------------------------

void Sensors::HandleAndForwardIncomingMeasurements(const char* data, size_t len, const char* from_ip)
{
    const bool ok = ProcessIncomingData(data, len, from_ip);
    if(!ok) return;

    for(const auto& target : Server::Get()->GetForwardTargets())
        utils::SendTcpBlocking(target.address, target.port, data, len, 300, true);
}

bool Sensors::ProcessIncomingData(const char* data, size_t len, const char* from_ip)
{
    try
    {
        auto fields = SensorDataParser::Parse(data, len);
        if(!fields || fields->size() < IDX_Max)
        {
            LOG(LogLevel::Warning, "Too few measurements from {} (len: {})", from_ip, len);
            return false;
        }
        HandleMeasurements(*fields);
    }
    catch(const std::exception& e)
    {
        LOG(LogLevel::Warning, "Exception parsing sensor data from {}: {}", from_ip, e.what());
        return false;
    }
    return true;
}

// ---------------------------------------------------------------------------
// Measurement accumulation
// ---------------------------------------------------------------------------

void Sensors::HandleMeasurements(const std::vector<std::string>& f)
{
#ifdef _WIN32
    float temp     = boost::lexical_cast<float>(f[IDX_BME680_TEMP]);
    float hum      = boost::lexical_cast<float>(f[IDX_BME680_HUM]);
    float pressure = boost::lexical_cast<float>(f[IDX_BME680_PRESSURE]);
    int   co2      = utils::stoi<int>(f[IDX_SCD_CO2]);
    int   pm25     = utils::stoi<int>(f[IDX_PM25]);
    int   pm10     = utils::stoi<int>(f[IDX_PM10]);
    int   uv       = utils::stoi<int>(f[IDX_UV]);
    int   co       = utils::stoi<int>(f[IDX_CO]);

    BsecHandler::Get()->AddMeasurementsAndCalculate(
        utils::stoi<int>(f[IDX_BME680_TIMESTAMP]),
        temp, pressure, hum,
        utils::stoi<int>(f[IDX_BME680_GAS_RESISTANCE]));
    float voc = BsecHandler::Get()->GetIaq();

    float r   = boost::lexical_cast<float>(f[IDX_R]);
    float g   = boost::lexical_cast<float>(f[IDX_G]);
    float b   = boost::lexical_cast<float>(f[IDX_B]);
    uint16_t lux = utils::stoi<int>(f[IDX_Lux]);
    uint16_t cct = utils::stoi<int>(f[IDX_CCT]);

    const auto now       = std::chrono::current_zone()->to_local(std::chrono::system_clock::now());
    Measurement incoming(temp, hum, co2, voc, co, pm25, pm10, pressure, r, g, b,
                         lux, cct, uv, std::format("{:%H:%M:%OS}", now));

    if(!m_currMeas)
    {
        m_currMeas             = std::make_unique<Measurement>(incoming);
        m_integrationStart     = std::chrono::steady_clock::now();
    }
    else
    {
        *m_currMeas += incoming;
    }

    UpdateGui(incoming);
    ++m_recvCount;

    const auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::steady_clock::now() - m_integrationStart).count();

    if(elapsed > m_integrationTime)
    {
        m_currMeas->Finalize();
        DatabaseLogic::Get()->InsertMeasurement(*m_currMeas);
        AddMeasurement(std::move(m_currMeas));
        m_currMeas = nullptr;
        UpdateDatabaseIfNeeded();
    }
#endif
}

void Sensors::AddMeasurement(std::unique_ptr<Measurement>&& meas)
{
    if(m_last_meas.size() >= m_graphResolution)
        m_last_meas.pop_front();
    m_last_meas.push_back(std::move(meas));
}

void Sensors::UpdateDatabaseIfNeeded()
{
    if(auto generated = DatabaseLogic::Get()->TakeGeneratedGraphs())
    {
        {
            std::scoped_lock lock(m_mtx);
            m_last_meas.clear();
            for(auto& measurement : generated->latest)
                m_last_meas.push_back(std::move(measurement));
            for(size_t index = 0; index < 3; ++index)
            {
                m_lastDay[index] = std::move(generated->day[index]);
                m_lastWeek[index] = std::move(generated->week[index]);
            }
        }
        WriteGraphs();
    }

    // Trigger graph regeneration once per interval.
    const auto elapsed_min = std::chrono::duration_cast<std::chrono::minutes>(
        std::chrono::steady_clock::now() - DatabaseLogic::Get()->GetLastUpdateTime()).count();
    if(elapsed_min >= m_graphGenerationInterval)
        DatabaseLogic::Get()->GenerateGraphs(m_graphResolution);
}

void Sensors::AddObserver(ISensorObserver* observer)
{
    std::scoped_lock lock(m_observerMutex);
    if(observer && std::find(m_observers.begin(), m_observers.end(), observer) == m_observers.end())
        m_observers.push_back(observer);
}

void Sensors::RemoveObserver(ISensorObserver* observer)
{
    std::scoped_lock lock(m_observerMutex);
    m_observers.erase(std::remove(m_observers.begin(), m_observers.end(), observer), m_observers.end());
}

void Sensors::UpdateGui(const Measurement& meas)
{
    std::scoped_lock lock(m_observerMutex);
    for(auto* obs : m_observers)
        obs->OnMeasurementUpdated(meas, m_recvCount);
}

void Sensors::ResetMeasurements()
{
    m_recvCount = 0;
    UpdateGui(Measurement{});
}

// ---------------------------------------------------------------------------
// Graph writing
// ---------------------------------------------------------------------------

// Appends time-label and field-value strings for every measurement in a container.
template<typename FieldType, typename Container>
void Sensors::CollectSeries(const Container& c, size_t offset,
                             std::string& labels, std::string& values)
{
    for(const auto& m : c)
    {
        labels += "'" + m->time + "',";
        // offsetof-based field access — keeps the graph engine independent of individual getters.
        const FieldType val = *reinterpret_cast<const FieldType*>(
            reinterpret_cast<const char*>(m.get()) + offset);
        values += std::to_string(val) + ",";
    }
}

template<typename T>
void Sensors::WriteGraph(const char* filename, uint16_t min_val, uint16_t max_val,
                          const char* name, size_t offset)
{
    if(m_templateStr.empty()) return;

    std::scoped_lock lock(m_mtx);

    std::string labels_latest, data_latest;
    std::string labels_day[3],  data_day[3];
    std::string labels_week[3], data_week[3];

    try
    {
        CollectSeries<T>(m_last_meas, offset, labels_latest, data_latest);
        for(int i = 0; i < 3; ++i)
        {
            CollectSeries<T>(m_lastDay[i],  offset, labels_day[i],  data_day[i]);
            CollectSeries<T>(m_lastWeek[i], offset, labels_week[i], data_week[i]);
        }
    }
    catch(...)
    {
        LOG(LogLevel::Error, "Exception collecting graph data for {}", filename);
        return;
    }

    std::ofstream out(std::string("Graphs/") + filename, std::ofstream::binary);
    if(!out)
    {
        LOG(LogLevel::Error, "Failed to open Graphs/{} for writing", filename);
        return;
    }

#ifdef _WIN32
    try
    {
        const std::string name_avg  = std::string(name) + " (Avg)";
        const std::string name_max  = std::string(name) + " (Max)";
        const std::string name_min  = std::string(name) + " (Min)";
        const std::string last_n    = "Last " + std::to_string(m_graphResolution) + " measurements";
        /*
        out << std::vformat(m_templateStr, std::make_format_args(
            min_val, max_val,
            labels_latest,   "Latest " + std::string(name) + " readings",
            "window.chartColors.orange", "window.chartColors.orange", data_latest,
            labels_day[0],
            name_avg, "window.chartColors.orange", "window.chartColors.orange", data_day[0],
            name_max, "window.chartColors.blue",   "window.chartColors.blue",   data_day[1],
            name_min, "window.chartColors.green",  "window.chartColors.green",  data_day[2],
            labels_week[0],
            name_avg, "window.chartColors.orange", "window.chartColors.orange", data_week[0],
            name_max, "window.chartColors.blue",   "window.chartColors.blue",   data_week[1],
            name_min, "window.chartColors.green",  "window.chartColors.green",  data_week[2],
            last_n, "Last Day", "Last Week"));
            */
    }
    catch(const std::exception& e)
    {
        LOG(LogLevel::Error, "Exception formatting graph {}: {}", filename, e.what());
    }
#endif
}

void Sensors::WriteGraphs()
{
    WriteGraph<decltype(Measurement::temp)>    ("Temperature.html", 15,   35,    "Temperature", offsetof(Measurement, temp));
    WriteGraph<decltype(Measurement::hum)>     ("Humidity.html",    0,    100,   "Humidity",    offsetof(Measurement, hum));
    WriteGraph<decltype(Measurement::co2)>     ("CO2.html",         150,  5000,  "CO2",         offsetof(Measurement, co2));
    WriteGraph<decltype(Measurement::voc)>     ("VOC.html",         0,    1000,  "VOC",         offsetof(Measurement, voc));
    WriteGraph<decltype(Measurement::co)>      ("CO.html",          0,    65535, "CO",          offsetof(Measurement, co));
    WriteGraph<decltype(Measurement::pm25)>    ("PM25.html",        0,    100,   "PM2.5",       offsetof(Measurement, pm25));
    WriteGraph<decltype(Measurement::pm10)>    ("PM10.html",        0,    100,   "PM10",        offsetof(Measurement, pm10));
    WriteGraph<decltype(Measurement::pressure)>("Pressure.html",    950,  1200,  "Pressure",    offsetof(Measurement, pressure));
    WriteGraph<decltype(Measurement::lux)>     ("Lux.html",         0,    10000, "Lux",         offsetof(Measurement, lux));
    WriteGraph<decltype(Measurement::cct)>     ("CCT.html",         0,    10000, "CCT",         offsetof(Measurement, cct));
    WriteGraph<decltype(Measurement::r)>       ("R.html",           0,    10000, "R",           offsetof(Measurement, r));
    WriteGraph<decltype(Measurement::g)>       ("G.html",           0,    10000, "G",           offsetof(Measurement, g));
    WriteGraph<decltype(Measurement::b)>       ("B.html",           0,    10000, "B",           offsetof(Measurement, b));
    WriteGraph<decltype(Measurement::uv)>      ("UV.html",          0,    10000, "UV",          offsetof(Measurement, uv));
}
