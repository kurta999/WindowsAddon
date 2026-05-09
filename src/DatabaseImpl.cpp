#include "pch.hpp"

Result::Result(sqlite3_stmt* stmt) :
    m_stmt(stmt)
{
}

Result::~Result()
{
    if(m_stmt)
        sqlite3_finalize(m_stmt);
}

int Result::GetColumnInt(int col) const
{
    return sqlite3_column_int(m_stmt, col);
}

std::string_view Result::GetColumnText(int col) const
{
    const auto* text = sqlite3_column_text(m_stmt, col);
    return text ? reinterpret_cast<const char*>(text) : std::string_view{};
}

int Result::GetColumnCount() const
{
    return sqlite3_column_count(m_stmt);
}

bool Result::StepNext()
{
    return sqlite3_step(m_stmt) == SQLITE_ROW;
}

Sqlite3Database::~Sqlite3Database()
{
    if(m_db)
        sqlite3_close(m_db);
}

bool Sqlite3Database::Open(const char* db_name)
{
    int ret = sqlite3_open(db_name, &m_db);
    if(ret != SQLITE_OK)
        LOG(LogLevel::Error, "Can't open database: {}", sqlite3_errmsg(m_db));
    return ret == SQLITE_OK;
}

bool Sqlite3Database::Close()
{
    int ret = sqlite3_close(m_db);
    if(ret != SQLITE_OK)
        LOG(LogLevel::Error, "Can't close database: {}", sqlite3_errmsg(m_db));
    else
        m_db = nullptr;
    return ret == SQLITE_OK;
}

void Sqlite3Database::ExecuteQuery(const std::string& query)
{
    char* zErrMsg = nullptr;
    int ret = sqlite3_exec(m_db, query.c_str(), nullptr, nullptr, &zErrMsg);
    if(ret != SQLITE_OK)
    {
        LOG(LogLevel::Error, "SQL Error: {}", zErrMsg);
        sqlite3_free(zErrMsg);
    }
}

int Sqlite3Database::ExecuteQueryAndGetLastId(const std::string& query)
{
    char* zErrMsg = nullptr;
    int ret = sqlite3_exec(m_db, query.c_str(), nullptr, nullptr, &zErrMsg);
    if(ret != SQLITE_OK)
    {
        LOG(LogLevel::Error, "SQL Error: {}", zErrMsg);
        sqlite3_free(zErrMsg);
        return -1;
    }
    return static_cast<int>(sqlite3_last_insert_rowid(m_db));
}

void Sqlite3Database::SendQueryAndFetch(const std::string& query, DbFetchCallback callback)
{
    sqlite3_stmt* stmt = nullptr;
    if(sqlite3_prepare_v2(m_db, query.c_str(), -1, &stmt, nullptr) == SQLITE_OK)
    {
        auto res = std::make_unique<Result>(stmt);
        callback(res);
    }
}

DBStream::DBStream(const char* db_name, IDatabase& db) :
    m_db(db)
{
    m_opened = m_db.Open(db_name);
}

DBStream::~DBStream()
{
    if(m_opened)
    {
        if(!m_db.Close())
            LOG(LogLevel::Error, "Failed to close database on DBStream destruction");
    }
}

DBStream::operator bool() const
{
    return m_opened;
}

void DBStream::ExecuteQuery(const std::string& query)
{
    m_db.ExecuteQuery(query);
}

int DBStream::ExecuteQueryAndGetLastId(const std::string& query)
{
    return m_db.ExecuteQueryAndGetLastId(query);
}

void DBStream::SendQueryAndFetch(const std::string& query, DbFetchCallback callback)
{
    m_db.SendQueryAndFetch(query, callback);
}
