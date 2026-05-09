#pragma once

#include "utils/CSingleton.hpp"
#include "interface/ISensorObserver.hpp"

#include <deque>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#pragma pack(push, 1)
class Measurement
{
public:
    Measurement() = default;

    Measurement(float _temp, float _hum, int _co2, float _voc, int _co, int _pm25, int _pm10,
                float _pressure, float _r, float _g, float _b, int _lux, int _cct, int _uv,
                std::string&& time_) :
        temp(_temp), hum(_hum), co2(_co2), voc(_voc), co(_co), pm25(_pm25), pm10(_pm10),
        pressure(_pressure), r(_r), g(_g), b(_b), lux(_lux), cct(_cct), uv(_uv),
        time(std::move(time_))
    {}

    ~Measurement() = default;

    Measurement(const Measurement& rhs) { *this += rhs; }

    Measurement& operator=(const Measurement& rhs)
    {
        temp = rhs.temp; hum = rhs.hum; pressure = rhs.pressure;
        r = rhs.r; g = rhs.g; b = rhs.b;
        co2 = rhs.co2; voc = rhs.voc; co = rhs.co;
        pm25 = rhs.pm25; pm10 = rhs.pm10;
        lux = rhs.lux; cct = rhs.cct; uv = rhs.uv;
        time = rhs.time;
        return *this;
    }

    Measurement& operator+=(const Measurement& rhs)
    {
        temp += rhs.temp; hum += rhs.hum; pressure += rhs.pressure;
        r += rhs.r; g += rhs.g; b += rhs.b;
        co2 += rhs.co2; voc += rhs.voc; co += rhs.co;
        pm25 += rhs.pm25; pm10 += rhs.pm10;
        lux += rhs.lux; cct += rhs.cct; uv += rhs.uv;
        cnt++;
        return *this;
    }

    void Finalize()
    {
        const auto n = static_cast<float>(cnt);
        temp /= n; hum /= n; pressure /= n;
        r /= n; g /= n; b /= n; voc /= n;
        co2 /= cnt; co /= cnt; pm25 /= cnt; pm10 /= cnt;
        lux /= cnt; cct /= cnt; uv /= cnt;
        cnt = 1;
    }

    float   temp = 0.f, hum = 0.f, voc = 0.f, pressure = 0.f, r = 0.f, g = 0.f, b = 0.f;
    int     co2 = 0, co = 0, pm25 = 0, pm10 = 0, lux = 0, cct = 0, uv = 0;
    std::string time;
    uint8_t cnt = 0;
};
#pragma pack(pop)

class Sensors : public CSingleton<Sensors>
{
    friend class CSingleton<Sensors>;

public:
    void Init();

    void AddObserver(ISensorObserver* observer);
    void RemoveObserver(ISensorObserver* observer);

    // Entry point for raw incoming TCP data; parses, accumulates, and forwards.
    void HandleAndForwardIncomingMeasurements(const char* data, size_t len, const char* from_ip);

    // Parse a raw sensor broadcast string and drive the accumulation pipeline.
    bool ProcessIncomingData(const char* data, size_t len, const char* from_ip);

    // Write all sensor metrics to their respective HTML graph files.
    void WriteGraphs();

    // Add a finalised measurement to the rolling display window.
    void AddMeasurement(std::unique_ptr<Measurement>&& meas);

    const std::deque<std::unique_ptr<Measurement>>& GetMeasurements() const { return m_last_meas; }

    void     SetGraphGenerationInterval(uint16_t v) { m_graphGenerationInterval = v; }
    uint16_t GetGraphGenerationInterval()     const { return m_graphGenerationInterval; }

    void     SetGraphResolution(uint16_t v) { m_graphResolution = v; }
    uint16_t GetGraphResolution()     const { return m_graphResolution; }

    void     SetIntegrationTime(uint16_t v) { m_integrationTime = v; }
    uint16_t GetIntegrationTime()     const { return m_integrationTime; }

    void ResetMeasurements();

    // Graph data filled by GraphGenerator (array order: avg[0], max[1], min[2]).
    std::vector<std::unique_ptr<Measurement>> last_day[3];
    std::vector<std::unique_ptr<Measurement>> last_week[3];

private:
    enum FieldIndex
    {
        IDX_SCD_TEMP,
        IDX_SCD_HUM,
        IDX_SCD_CO2,
        IDX_CO,
        IDX_BME680_TEMP,
        IDX_BME680_HUM,
        IDX_BME680_PRESSURE,
        IDX_BME680_GAS_RESISTANCE,
        IDX_BME680_TIMESTAMP,
        IDX_PM25,
        IDX_PM10,
        IDX_UV,
        IDX_R,
        IDX_G,
        IDX_B,
        IDX_CCT,
        IDX_Lux,
        IDX_Max
    };

    void HandleMeasurements(const std::vector<std::string>& fields);
    void UpdateGui(const Measurement& m);
    void UpdateDatabaseIfNeeded();

    // Graph-writing helpers (templated to work with any Measurement field type).
    template<typename FieldType, typename Container>
    static void CollectSeries(const Container& c, size_t offset,
                               std::string& labels, std::string& values);

    template<typename FieldType>
    void WriteGraph(const char* filename, uint16_t min_val, uint16_t max_val,
                    const char* name, size_t offset);

    // --- state ---
    std::vector<ISensorObserver*>          m_observers;
    std::unique_ptr<Measurement>           m_currMeas;
    std::chrono::steady_clock::time_point  m_integrationStart;
    std::deque<std::unique_ptr<Measurement>> m_last_meas;
    std::string                            m_templateStr;
    std::mutex                             m_mtx;
    size_t                                 m_recvCount = 0;

    uint16_t m_graphGenerationInterval = 10;
    uint16_t m_graphResolution         = 150;
    uint16_t m_integrationTime         = 10;
};
