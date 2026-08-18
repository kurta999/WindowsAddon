#pragma once

#include <chrono>
#include <cstdint>
#include <expected>
#include <optional>
#include <span>
#include <vector>

namespace modbus
{
enum class Error
{
    Timeout,
    Crc,
    Exception,
    Malformed,
    WrongSlave,
    WrongFunction
};

struct Failure
{
    Error error{};
    std::uint8_t exception_code{};

    bool operator==(const Failure&) const = default;
};

class ITransport
{
public:
    virtual ~ITransport() = default;
    virtual std::optional<std::vector<std::uint8_t>> Transact(
        std::span<const std::uint8_t> request, std::chrono::milliseconds timeout) = 0;
};

class Client
{
public:
    explicit Client(ITransport& transport) : m_transport(transport) {}

    void SetTimeout(std::chrono::milliseconds timeout) { m_timeout = timeout; }
    void SetRetries(unsigned retries) { m_retries = retries; }

    [[nodiscard]] std::expected<std::vector<bool>, Failure> ReadCoils(
        std::uint8_t slave, std::uint16_t offset, std::uint16_t count);
    [[nodiscard]] std::expected<std::vector<bool>, Failure> ReadDiscreteInputs(
        std::uint8_t slave, std::uint16_t offset, std::uint16_t count);
    [[nodiscard]] std::expected<std::vector<std::uint16_t>, Failure> ReadHoldingRegisters(
        std::uint8_t slave, std::uint16_t offset, std::uint16_t count);
    [[nodiscard]] std::expected<std::vector<std::uint16_t>, Failure> ReadInputRegisters(
        std::uint8_t slave, std::uint16_t offset, std::uint16_t count);

private:
    [[nodiscard]] std::expected<std::vector<std::uint8_t>, Failure> Read(
        std::uint8_t slave, std::uint8_t function, std::uint16_t offset, std::uint16_t count);

    ITransport& m_transport;
    std::chrono::milliseconds m_timeout{1000};
    unsigned m_retries = 0;
};
}
