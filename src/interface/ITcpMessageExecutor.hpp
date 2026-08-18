#pragma once

#include <memory>
#include <span>
#include <tuple>
#include <string>

class Session;
using SharedSession = std::shared_ptr<Session>;
using TcpMessageReturn = std::tuple<bool, bool, std::string>;

class ITcpMessageExecutor
{
public:
    virtual ~ITcpMessageExecutor() = default;

    virtual TcpMessageReturn Process(const SharedSession& session, std::span<char> message) = 0;
};
