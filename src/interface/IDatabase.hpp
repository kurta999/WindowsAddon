#pragma once

#include <string>
#include <functional>
#include <memory>

class Result;

using DbFetchCallback = std::function<void(std::unique_ptr<Result>&)>;

class IDatabase
{
public:
    virtual ~IDatabase() = default;

    [[nodiscard]] virtual bool Open(const char* db_name) = 0;
    [[nodiscard]] virtual bool Close() = 0;
    virtual void ExecuteQuery(const std::string& query) = 0;
    [[nodiscard]] virtual int ExecuteQueryAndGetLastId(const std::string& query) = 0;
    virtual void SendQueryAndFetch(const std::string& query, DbFetchCallback callback) = 0;
};
