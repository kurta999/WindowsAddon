#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

struct StoredTimeEntry
{
    int id{};
    std::int64_t start{};
    std::int64_t end{};
    std::string comment;

    bool operator==(const StoredTimeEntry&) const = default;
};

// Persistence port used by the time-tracker domain service. Implementations
// may use SQLite, an in-memory fake, or another durable store.
class ITimeTrackerStorage
{
public:
    virtual ~ITimeTrackerStorage() = default;

    [[nodiscard]] virtual bool IsOpen() const = 0;
    [[nodiscard]] virtual const std::string& LastError() const = 0;
    [[nodiscard]] virtual std::optional<int> Insert(
        std::int64_t start, std::int64_t end, const std::string& comment) = 0;
    [[nodiscard]] virtual bool Update(
        int id, std::int64_t start, std::int64_t end, const std::string& comment) = 0;
    [[nodiscard]] virtual bool Remove(int id) = 0;
    [[nodiscard]] virtual std::vector<StoredTimeEntry> LoadRange(
        std::int64_t first_start, std::int64_t last_start) = 0;
};
