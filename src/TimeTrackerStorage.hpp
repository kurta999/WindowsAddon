#pragma once

#include "interface/ITimeTrackerStorage.hpp"

#include <filesystem>

struct sqlite3;

// Headless persistence boundary for TimeTracker.  Keeping the SQLite connection
// open also makes ":memory:" databases useful in tests and avoids interpolating
// user-provided comments into SQL.
class TimeTrackerStorage final : public ITimeTrackerStorage
{
public:
    explicit TimeTrackerStorage(const std::filesystem::path& database_path);
    ~TimeTrackerStorage() override;

    TimeTrackerStorage(const TimeTrackerStorage&) = delete;
    TimeTrackerStorage& operator=(const TimeTrackerStorage&) = delete;

    [[nodiscard]] bool IsOpen() const override { return m_db != nullptr; }
    [[nodiscard]] const std::string& LastError() const override { return m_lastError; }

    [[nodiscard]] std::optional<int> Insert(
        std::int64_t start, std::int64_t end, const std::string& comment) override;
    [[nodiscard]] bool Update(
        int id, std::int64_t start, std::int64_t end, const std::string& comment) override;
    [[nodiscard]] bool Remove(int id) override;
    [[nodiscard]] std::vector<StoredTimeEntry> LoadRange(
        std::int64_t first_start, std::int64_t last_start) override;

    // Used by imports and tests. A bad row rolls back the complete replacement.
    [[nodiscard]] bool ReplaceAll(const std::vector<StoredTimeEntry>& entries);

private:
    [[nodiscard]] bool Execute(const char* sql);
    void SetError(const char* operation);

    sqlite3* m_db = nullptr;
    std::string m_lastError;
};
