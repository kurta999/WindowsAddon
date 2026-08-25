#include "pch_core.hpp"
#include "Sensors.hpp"
#include "Logger.hpp"
#include "Utils.hpp"
#include <regex>
#include "BsecHandler.hpp"
#include "DatabaseLogic.hpp"
#include "Server.hpp"
#include "SensorDataParser.hpp"
#include "SettingsReader.hpp"
#include "SettingsWriter.hpp"
#include <ostream>
#include "utils/XmlDocument.hpp"

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

    if(m_Server == nullptr)
        return;

    for(const auto& target : m_Server->GetForwardTargets())
        utils::SendTcpBlocking(target.address, static_cast<uint16_t>(target.port), data, len, 300, true);
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

namespace
{
// Local wall-clock time as HH:MM:SS. Falls back to UTC where the platform has
// no time zone database, so the sensor pipeline stays portable.
std::string FormatLocalTimestamp()
{
    const auto now = std::chrono::system_clock::now();
    try
    {
        return std::format("{:%H:%M:%OS}", std::chrono::current_zone()->to_local(now));
    }
    catch(const std::exception&)
    {
        return std::format("{:%H:%M:%OS}", std::chrono::floor<std::chrono::seconds>(now));
    }
}
}

void Sensors::HandleMeasurements(const std::vector<std::string>& f)
{
    float temp     = boost::lexical_cast<float>(f[IDX_BME680_TEMP]);
    float hum      = boost::lexical_cast<float>(f[IDX_BME680_HUM]);
    float pressure = boost::lexical_cast<float>(f[IDX_BME680_PRESSURE]);
    int   co2      = utils::stoi<int>(f[IDX_SCD_CO2]);
    int   pm25     = utils::stoi<int>(f[IDX_PM25]);
    int   pm10     = utils::stoi<int>(f[IDX_PM10]);
    int   uv       = utils::stoi<int>(f[IDX_UV]);
    int   co       = utils::stoi<int>(f[IDX_CO]);

    float voc = 0.0f;
    if(m_Bsec != nullptr)
    {
        m_Bsec->AddMeasurementsAndCalculate(
            utils::stoi<int>(f[IDX_BME680_TIMESTAMP]),
            temp, pressure, hum,
            static_cast<float>(utils::stoi<int>(f[IDX_BME680_GAS_RESISTANCE])));
        voc = m_Bsec->GetIaq();
    }

    float r   = boost::lexical_cast<float>(f[IDX_R]);
    float g   = boost::lexical_cast<float>(f[IDX_G]);
    float b   = boost::lexical_cast<float>(f[IDX_B]);
    uint16_t lux = utils::stoi<uint16_t>(f[IDX_Lux]);
    uint16_t cct = utils::stoi<uint16_t>(f[IDX_CCT]);

    Measurement incoming(temp, hum, co2, voc, co, pm25, pm10, pressure, r, g, b,
                         lux, cct, uv, FormatLocalTimestamp());

    if(!m_currMeas)
    {
        m_currMeas             = std::make_unique<Measurement>(incoming);
        m_integrationStart     = std::chrono::steady_clock::now();
    }
    else
    {
        *m_currMeas += incoming;
    }

    /* Counted before the notification, not after: the panel shows this value,
       and notifying first left it one measurement behind for ever. */
    ++m_recvCount;
    UpdateGui(incoming);

    const auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::steady_clock::now() - m_integrationStart).count();

    if(elapsed > m_integrationTime)
    {
        m_currMeas->Finalize();
        if(m_Database != nullptr)
            m_Database->InsertMeasurement(*m_currMeas);
        AddMeasurement(std::move(m_currMeas));
        m_currMeas = nullptr;
        UpdateDatabaseIfNeeded();
    }
}

void Sensors::AddMeasurement(std::unique_ptr<Measurement>&& meas)
{
    if(m_last_meas.size() >= m_graphResolution)
        m_last_meas.pop_front();
    m_last_meas.push_back(std::move(meas));
}

void Sensors::UpdateDatabaseIfNeeded()
{
    if(m_Database == nullptr)
        return;

    if(auto generated = m_Database->TakeGeneratedGraphs())
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
        std::chrono::steady_clock::now() - m_Database->GetLastUpdateTime()).count();
    if(elapsed_min >= m_graphGenerationInterval)
        m_Database->GenerateGraphs(m_graphResolution);
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
void Sensors::CollectSeries(const Container& c, FieldType Measurement::* field,
                             std::string& labels, std::string& values)
{
    for(const auto& m : c)
    {
        if(!m) continue;
        labels += "'" + m->time + "',";
        values += std::to_string((*m).*field) + ",";
    }
}

template<typename T>
void Sensors::WriteGraph(const char* filename, uint16_t min_val, uint16_t max_val,
                          const char* name, T Measurement::* field)
{
    if(m_templateStr.empty()) return;

    std::scoped_lock lock(m_mtx);

    std::string labels_latest, data_latest;
    std::string labels_day[3],  data_day[3];
    std::string labels_week[3], data_week[3];

    try
    {

        CollectSeries<T>(m_last_meas, field, labels_latest, data_latest);
        for(int i = 0; i < 3; ++i)
        {
            CollectSeries<T>(m_lastDay[i],  field, labels_day[i],  data_day[i]);
            CollectSeries<T>(m_lastWeek[i], field, labels_week[i], data_week[i]);
        }
    }
    catch(const std::exception& e)
    {
        LOG(LogLevel::Error, "Exception collecting graph data for {}: {}", filename, e.what());
        return;
    }

    /* Rendered before the file is touched. Opening first truncated every graph
       to zero bytes whenever this step did not produce output. */
    std::string document;
    try
    {
        const std::string name_avg = std::string(name) + " (Avg)";
        const std::string name_max = std::string(name) + " (Max)";
        const std::string name_min = std::string(name) + " (Min)";
        const std::string last_n = "Last " + std::to_string(m_graphResolution) + " measurements";
        const std::string title_latest = "Latest " + std::string(name) + " readings";
        const std::string color_orange = "window.chartColors.orange";
        const std::string color_blue = "window.chartColors.blue";
        const std::string color_green = "window.chartColors.green";
        const std::string title_day = "Last Day";
        const std::string title_week = "Last Week";

        /* Every argument is a named lvalue on purpose: std::make_format_args
           does not bind temporaries, which is what stopped this compiling. */
        document = std::vformat(m_templateStr, std::make_format_args(
            min_val, max_val,
            labels_latest, title_latest, color_orange, color_orange, data_latest,
            labels_day[0],
            name_avg, color_orange, color_orange, data_day[0],
            name_max, color_blue,   color_blue,   data_day[1],
            name_min, color_green,  color_green,  data_day[2],
            labels_week[0],
            name_avg, color_orange, color_orange, data_week[0],
            name_max, color_blue,   color_blue,   data_week[1],
            name_min, color_green,  color_green,  data_week[2],
            last_n, title_day, title_week));
    }
    catch(const std::exception& e)
    {
        LOG(LogLevel::Error, "Exception formatting graph {}: {}", filename, e.what());
        return;
    }

    std::ofstream out(std::string("Graphs/") + filename, std::ofstream::binary);
    if(!out)
    {
        LOG(LogLevel::Error, "Failed to open Graphs/{} for writing", filename);
        return;
    }
    out << document;
}

void Sensors::WriteGraphs()
{
    WriteGraph("Temperature.html", 15,   35,    "Temperature", &Measurement::temp);
    WriteGraph("Humidity.html",    0,    100,   "Humidity",    &Measurement::hum);
    WriteGraph("CO2.html",         150,  5000,  "CO2",         &Measurement::co2);
    WriteGraph("VOC.html",         0,    1000,  "VOC",         &Measurement::voc);
    WriteGraph("CO.html",          0,    65535, "CO",          &Measurement::co);
    WriteGraph("PM25.html",        0,    100,   "PM2.5",       &Measurement::pm25);
    WriteGraph("PM10.html",        0,    100,   "PM10",        &Measurement::pm10);
    WriteGraph("Pressure.html",    950,  1200,  "Pressure",    &Measurement::pressure);
    WriteGraph("Lux.html",         0,    10000, "Lux",         &Measurement::lux);
    WriteGraph("CCT.html",         0,    10000, "CCT",         &Measurement::cct);
    WriteGraph("R.html",           0,    10000, "R",           &Measurement::r);
    WriteGraph("G.html",           0,    10000, "G",           &Measurement::g);
    WriteGraph("B.html",           0,    10000, "B",           &Measurement::b);
    WriteGraph("UV.html",          0,    10000, "UV",          &Measurement::uv);
}

/* Five of the nine keys in this block belong to the TCP server. They stay here
   because that is where every settings.ini in the wild has them, but the server
   reads and writes them itself now - this used to spell its field names through
   Server::Get(), nine times, which was one half of the cycle between the two. */
void Sensors::LoadSettings(SettingsReader& reader)
{
    if(m_Server != nullptr)
        m_Server->LoadSettingsFrom(reader, "Sensors");

    SetGraphGenerationInterval(utils::stoi<uint16_t>(reader.Required("Sensors", "GraphGenerationInterval")));
    SetGraphResolution(utils::stoi<uint16_t>(reader.Required("Sensors", "GraphResolution")));
    SetIntegrationTime(utils::stoi<uint16_t>(reader.Required("Sensors", "IntegrationTime")));
}

void Sensors::SaveSettings(std::ostream& out) const
{
    SettingsWriter writer(out, "Sensors");
    if(m_Server != nullptr)
        m_Server->WriteSettingsTo(writer);

    writer.Key("GraphGenerationInterval", GetGraphGenerationInterval(), "Minutes")
        .Key("GraphResolution", GetGraphResolution(), "Number of different measurement points in generated graph")
        .Key("IntegrationTime", GetIntegrationTime(), "Seconds");

    if(m_Server != nullptr)
        m_Server->WriteForwardSettingsTo(writer);

    writer.Blank();
}
