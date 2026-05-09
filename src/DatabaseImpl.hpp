#pragma once

#include <string>

#include <sqlite3.h>

#include "IDatabase.hpp"

class Result
{
public:
    explicit Result(sqlite3_stmt* stmt);
    ~Result();

    Result(const Result&) = delete;
    Result& operator=(const Result&) = delete;
    Result(Result&&) = delete;
    Result& operator=(Result&&) = delete;

    [[nodiscard]] int GetColumnInt(int col) const;
    [[nodiscard]] std::string_view GetColumnText(int col) const;
    [[nodiscard]] int GetColumnCount() const;
    [[nodiscard]] bool StepNext();

private:
    sqlite3_stmt* m_stmt = nullptr;
};

class Sqlite3Database final : public IDatabase
{
public:
    Sqlite3Database() = default;
    ~Sqlite3Database() override;

    Sqlite3Database(const Sqlite3Database&) = delete;
    Sqlite3Database& operator=(const Sqlite3Database&) = delete;

    [[nodiscard]] bool Open(const char* db_name) override;
    [[nodiscard]] bool Close() override;
    void ExecuteQuery(const std::string& query) override;
    [[nodiscard]] int ExecuteQueryAndGetLastId(const std::string& query) override;
    void SendQueryAndFetch(const std::string& query, DbFetchCallback callback) override;

private:
    sqlite3* m_db = nullptr;
};

class DBStream
{
public:
    DBStream(const char* db_name, IDatabase& db);
    ~DBStream();

    DBStream(const DBStream&) = delete;
    DBStream& operator=(const DBStream&) = delete;

    [[nodiscard]] explicit operator bool() const;
    void ExecuteQuery(const std::string& query);
    [[nodiscard]] int ExecuteQueryAndGetLastId(const std::string& query);
    void SendQueryAndFetch(const std::string& query, DbFetchCallback callback);

private:
    IDatabase& m_db;
    bool m_opened = false;
};
