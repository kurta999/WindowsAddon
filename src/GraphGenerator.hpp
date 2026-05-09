#pragma once

#include "interface/IDatabase.hpp"

#include <atomic>
#include <chrono>
#include <future>
#include <cstdint>

class Measurement;
class Result;
class Sensors;

// Owns the async graph-data queries against the measurements DB.
// SRP: the only reason to change this class is if the SQL queries or graph-data shape change.
// DIP: depends on IDatabase, not on a concrete SQLite type.
class GraphGenerator
{
public:
    GraphGenerator(const char* db_name, IDatabase& db);
    ~GraphGenerator();

    // Launch async generation; no-op if a previous run is still in flight.
    void Generate();

    // Block until any in-flight generation finishes (call before inserting new rows).
    void WaitIfRunning();

    // Signal cancellation and wait for shutdown (called from destructor / DatabaseLogic dtor).
    void Shutdown();

    void     SetGraphHours(uint8_t slot, uint32_t hours);
    uint32_t GetGraphHours(uint8_t slot) const;

    std::chrono::steady_clock::time_point last_update{};

private:
    void DoGenerate();

    void QueryLatest(std::unique_ptr<Result>& result);
    void QueryMeasFromPast(std::unique_ptr<Result>& result, std::vector<std::unique_ptr<Measurement>>* out);

    // Shared row-to-Measurement mapping; base is the column offset (2 for Latest, 0 for MeasFromPast).
    static Measurement ParseRow(Result& result, int base);

    const char*          m_db_name;
    IDatabase&           m_db;
    std::future<void>    m_future;
    std::atomic<bool>    m_destructing{false};
    uint32_t             m_hours_1 = 24;
    uint32_t             m_hours_2 = 24 * 7;
};
