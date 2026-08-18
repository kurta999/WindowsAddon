#pragma once

#include "utils/CSingleton.hpp"
#include "interface/ISensorObserver.hpp"
#include "Measurement.hpp"

#include <deque>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

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
    std::mutex                             m_observerMutex;
    std::unique_ptr<Measurement>           m_currMeas;
    std::chrono::steady_clock::time_point  m_integrationStart;
    std::deque<std::unique_ptr<Measurement>> m_last_meas;
    std::string                            m_templateStr;
    std::mutex                             m_mtx;
    // Graph aggregation order: average [0], maximum [1], minimum [2].
    std::vector<std::unique_ptr<Measurement>> m_lastDay[3];
    std::vector<std::unique_ptr<Measurement>> m_lastWeek[3];
    size_t                                 m_recvCount = 0;

    uint16_t m_graphGenerationInterval = 10;
    uint16_t m_graphResolution         = 150;
    uint16_t m_integrationTime         = 10;
};
