#include "pch.hpp"

constexpr const char* db_name = "meas_data.db";

DatabaseLogic::DatabaseLogic()
{
    m_db         = std::make_unique<Sqlite3Database>();
    m_repository = std::make_unique<MeasurementRepository>(db_name, *m_db);
    m_generator  = std::make_unique<GraphGenerator>(db_name, *m_db);
}

DatabaseLogic::~DatabaseLogic() = default;

void DatabaseLogic::GenerateGraphs(uint16_t resolution)
{
    m_generator->Generate(resolution);
}

std::optional<GraphData> DatabaseLogic::TakeGeneratedGraphs()
{
    return m_generator->TakeCompleted();
}

void DatabaseLogic::InsertMeasurement(const Measurement& measurement)
{
    m_generator->WaitIfRunning();  // serialise: no DB reads while inserting
    m_repository->Insert(measurement);
}

void DatabaseLogic::SetGraphHours(uint8_t slot, uint32_t hours)
{
    m_generator->SetGraphHours(slot, hours);
}

uint32_t DatabaseLogic::GetGraphHours(uint8_t slot) const
{
    return m_generator->GetGraphHours(slot);
}

std::chrono::steady_clock::time_point DatabaseLogic::GetLastUpdateTime() const
{
    return m_generator->GetLastUpdateTime();
}
