#pragma once

#include "interface/IDatabase.hpp"
#include "DatabaseImpl.hpp"
#include "Measurement.hpp"

class Measurement;

// Owns the DB schema and the insert operation for sensor measurements.
// SRP: the only reason to change this class is if the measurements table schema changes.
class MeasurementRepository
{
public:
    MeasurementRepository(const char* db_name, IDatabase& db);

    void Insert(const Measurement& m);

private:
    const char* m_db_name;
    IDatabase&  m_db;
};
