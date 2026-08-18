#include "TimeTrackerStorage.hpp"

#include <sqlite3.h>

#include <limits>

namespace
{
class Statement
{
public:
    Statement(sqlite3* db, const char* sql)
    {
        if(sqlite3_prepare_v2(db, sql, -1, &m_stmt, nullptr) != SQLITE_OK)
            m_stmt = nullptr;
    }

    ~Statement()
    {
        if(m_stmt)
            sqlite3_finalize(m_stmt);
    }

    sqlite3_stmt* Get() const { return m_stmt; }
    explicit operator bool() const { return m_stmt != nullptr; }

private:
    sqlite3_stmt* m_stmt = nullptr;
};

bool BindEntry(sqlite3_stmt* statement, const StoredTimeEntry& entry, bool include_id)
{
    int index = 1;
    if(include_id && sqlite3_bind_int(statement, index++, entry.id) != SQLITE_OK)
        return false;
    return sqlite3_bind_int64(statement, index++, entry.start) == SQLITE_OK &&
           sqlite3_bind_int64(statement, index++, entry.end) == SQLITE_OK &&
           sqlite3_bind_text(statement, index, entry.comment.c_str(),
               static_cast<int>(entry.comment.size()), SQLITE_TRANSIENT) == SQLITE_OK;
}
}

TimeTrackerStorage::TimeTrackerStorage(const std::filesystem::path& database_path)
{
    const auto path = database_path.string();
    if(sqlite3_open(path.c_str(), &m_db) != SQLITE_OK)
    {
        SetError("open");
        if(m_db)
            sqlite3_close(m_db);
        m_db = nullptr;
        return;
    }

    // Deliberately do not constrain end >= start: legacy/incomplete rows and
    // temporarily invalid GUI edits must remain loadable so the UI can flag and
    // repair them later.
    if(!Execute("CREATE TABLE IF NOT EXISTS time_table("
                "id INTEGER PRIMARY KEY,"
                "start INTEGER NOT NULL,"
                "end INTEGER NOT NULL,"
                "comment TEXT NOT NULL);"))
    {
        sqlite3_close(m_db);
        m_db = nullptr;
    }
}

TimeTrackerStorage::~TimeTrackerStorage()
{
    if(m_db)
        sqlite3_close(m_db);
}

std::optional<int> TimeTrackerStorage::Insert(std::int64_t start, std::int64_t end, const std::string& comment)
{
    if(!m_db)
        return std::nullopt;

    Statement statement(m_db, "INSERT INTO time_table(start, end, comment) VALUES(?, ?, ?);");
    const StoredTimeEntry entry{0, start, end, comment};
    if(!statement || !BindEntry(statement.Get(), entry, false) || sqlite3_step(statement.Get()) != SQLITE_DONE)
    {
        SetError("insert");
        return std::nullopt;
    }

    const sqlite3_int64 id = sqlite3_last_insert_rowid(m_db);
    if(id > std::numeric_limits<int>::max())
    {
        m_lastError = "insert: row id is outside the supported range";
        return std::nullopt;
    }
    return static_cast<int>(id);
}

bool TimeTrackerStorage::Update(int id, std::int64_t start, std::int64_t end, const std::string& comment)
{
    if(!m_db)
        return false;

    Statement statement(m_db, "UPDATE time_table SET start = ?, end = ?, comment = ? WHERE id = ?;");
    if(!statement ||
       sqlite3_bind_int64(statement.Get(), 1, start) != SQLITE_OK ||
       sqlite3_bind_int64(statement.Get(), 2, end) != SQLITE_OK ||
       sqlite3_bind_text(statement.Get(), 3, comment.c_str(), static_cast<int>(comment.size()), SQLITE_TRANSIENT) != SQLITE_OK ||
       sqlite3_bind_int(statement.Get(), 4, id) != SQLITE_OK ||
       sqlite3_step(statement.Get()) != SQLITE_DONE)
    {
        SetError("update");
        return false;
    }
    return sqlite3_changes(m_db) == 1;
}

bool TimeTrackerStorage::Remove(int id)
{
    if(!m_db)
        return false;

    Statement statement(m_db, "DELETE FROM time_table WHERE id = ?;");
    if(!statement || sqlite3_bind_int(statement.Get(), 1, id) != SQLITE_OK ||
       sqlite3_step(statement.Get()) != SQLITE_DONE)
    {
        SetError("delete");
        return false;
    }
    return sqlite3_changes(m_db) == 1;
}

std::vector<StoredTimeEntry> TimeTrackerStorage::LoadRange(std::int64_t first_start, std::int64_t last_start)
{
    std::vector<StoredTimeEntry> entries;
    if(!m_db)
        return entries;

    Statement statement(m_db,
        "SELECT id, start, end, comment FROM time_table "
        "WHERE start >= ? AND start <= ? ORDER BY start ASC, id ASC;");
    if(!statement ||
       sqlite3_bind_int64(statement.Get(), 1, first_start) != SQLITE_OK ||
       sqlite3_bind_int64(statement.Get(), 2, last_start) != SQLITE_OK)
    {
        SetError("select");
        return entries;
    }

    int result = SQLITE_ROW;
    while((result = sqlite3_step(statement.Get())) == SQLITE_ROW)
    {
        const auto* text = sqlite3_column_text(statement.Get(), 3);
        entries.push_back({
            sqlite3_column_int(statement.Get(), 0),
            sqlite3_column_int64(statement.Get(), 1),
            sqlite3_column_int64(statement.Get(), 2),
            text ? reinterpret_cast<const char*>(text) : ""
        });
    }
    if(result != SQLITE_DONE)
        SetError("select");
    return entries;
}

bool TimeTrackerStorage::ReplaceAll(const std::vector<StoredTimeEntry>& entries)
{
    if(!m_db || !Execute("BEGIN IMMEDIATE TRANSACTION;"))
        return false;

    bool ok = Execute("DELETE FROM time_table;");
    Statement statement(m_db, "INSERT INTO time_table(id, start, end, comment) VALUES(?, ?, ?, ?);");
    for(const auto& entry : entries)
    {
        if(!ok || !statement || !BindEntry(statement.Get(), entry, true) ||
           sqlite3_step(statement.Get()) != SQLITE_DONE)
        {
            SetError("replace");
            ok = false;
            break;
        }
        sqlite3_reset(statement.Get());
        sqlite3_clear_bindings(statement.Get());
    }

    if(ok && Execute("COMMIT;"))
        return true;

    (void)Execute("ROLLBACK;");
    return false;
}

bool TimeTrackerStorage::Execute(const char* sql)
{
    if(!m_db)
        return false;
    if(sqlite3_exec(m_db, sql, nullptr, nullptr, nullptr) == SQLITE_OK)
        return true;
    SetError("execute");
    return false;
}

void TimeTrackerStorage::SetError(const char* operation)
{
    m_lastError = operation;
    m_lastError += ": ";
    m_lastError += m_db ? sqlite3_errmsg(m_db) : "database is not open";
}
