#pragma once

#include "interface/IMeasurementSink.hpp"

#include "utils/CSingleton.hpp"
#include "interface/ISensorObserver.hpp"
#include "Measurement.hpp"

#include <deque>
#include <memory>
#include <mutex>
#include <string>
#include <vector>
#include "interface/ISettingsBinding.hpp"
#include <iosfwd>
#include <string_view>

class Server;
class DatabaseLogic;
class BsecHandler;

class Sensors : public CSingleton<Sensors>, public ISettingsBinding, public IMeasurementSink
{
    friend class CSingleton<Sensors>;

public:
    // !\brief The TCP server this coordinates. Supplied by the composition
    // root before settings are read: five of the keys in this subsystem's
    // block belong to the server, and it configures the server on load.
    void SetServer(Server& server) noexcept { m_Server = &server; }

    // !\brief The store the accumulated measurements go to, and the air-quality
    // library that turns a gas reading into an IAQ index. The sensor worker
    // fetched both from its own thread, six times, on every broadcast.
    void SetDatabase(DatabaseLogic& database) noexcept { m_Database = &database; }
    void SetBsec(BsecHandler& bsec) noexcept { m_Bsec = &bsec; }

    // ISettingsBinding - this subsystem owns its own block of settings.ini.
    [[nodiscard]] std::string_view SettingsSection() const override { return "Sensors"; }
    void LoadSettings(SettingsReader& reader) override;
    void SaveSettings(std::ostream& out) const override;

    void Init();

    void AddObserver(ISensorObserver* observer);
    void RemoveObserver(ISensorObserver* observer);

    // Entry point for raw incoming TCP data; parses, accumulates, and forwards.
    void HandleAndForwardIncomingMeasurements(const char* data, size_t len, const char* from_ip);

    // IMeasurementSink - the same operation, named for the port so a caller
    // can forward a broadcast without knowing this class exists.
    void HandleIncomingMeasurements(const char* data, std::size_t len, const char* source) override
    {
        HandleAndForwardIncomingMeasurements(data, len, source);
    }

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
    Server* m_Server = nullptr;
    DatabaseLogic* m_Database = nullptr;
    BsecHandler* m_Bsec = nullptr;

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

    // Graph-writing helpers. The field is named with a pointer-to-member so a
    // reordered or retyped Measurement member is a compile error rather than a
    // silent misread through offsetof + reinterpret_cast.
    template<typename FieldType, typename Container>
    static void CollectSeries(const Container& c, FieldType Measurement::* field,
                               std::string& labels, std::string& values);

    template<typename FieldType>
    void WriteGraph(const char* filename, uint16_t min_val, uint16_t max_val,
                    const char* name, FieldType Measurement::* field);

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
