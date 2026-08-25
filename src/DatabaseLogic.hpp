#pragma once

#include "utils/CSingleton.hpp"
#include "MeasurementRepository.hpp"
#include "GraphGenerator.hpp"
#include "interface/IDatabase.hpp"

#include <chrono>
#include <cstdint>
#include <memory>
#include "DatabaseImpl.hpp"
#include "interface/ISettingsBinding.hpp"
#include <iosfwd>
#include <string_view>

class Measurement;

// Thin coordinator: owns the DB connection and delegates to focused sub-components.
// To add a new persistence concern, add a new component — do not grow this class.
class DatabaseLogic : public CSingleton<DatabaseLogic>, public ISettingsBinding
{
    friend class CSingleton<DatabaseLogic>;

public:
    // ISettingsBinding - this subsystem owns its own block of settings.ini.
    [[nodiscard]] std::string_view SettingsSection() const override { return "Graph"; }
    void LoadSettings(SettingsReader& reader) override;
    void SaveSettings(std::ostream& out) const override;

    DatabaseLogic();
    ~DatabaseLogic();

    // Trigger async graph regeneration (no-op while a previous run is in flight).
    void GenerateGraphs(uint16_t resolution);

    std::optional<GraphData> TakeGeneratedGraphs();

    // Insert one averaged measurement. Blocks if graph generation is running.
    void InsertMeasurement(const Measurement& measurement);

    void     SetGraphHours(uint8_t slot, uint32_t hours);
    uint32_t GetGraphHours(uint8_t slot) const;

    std::chrono::steady_clock::time_point GetLastUpdateTime() const;

private:
    std::unique_ptr<IDatabase>             m_db;
    std::unique_ptr<MeasurementRepository> m_repository;
    std::unique_ptr<GraphGenerator>        m_generator;
};
