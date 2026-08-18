#include "ModbusClient.hpp"

#include "CanCodecs.hpp"

#include <algorithm>

namespace modbus
{
namespace
{
void AppendU16(std::vector<std::uint8_t>& bytes, std::uint16_t value)
{
    bytes.push_back(static_cast<std::uint8_t>(value >> 8));
    bytes.push_back(static_cast<std::uint8_t>(value));
}

void AppendCrc(std::vector<std::uint8_t>& bytes)
{
    const auto crc = can_codec::ModbusCrc(bytes);
    bytes.push_back(static_cast<std::uint8_t>(crc));
    bytes.push_back(static_cast<std::uint8_t>(crc >> 8));
}
}

std::expected<std::vector<bool>, Failure> Client::ReadCoils(
    std::uint8_t slave, std::uint16_t offset, std::uint16_t count)
{
    auto response = Read(slave, 1, offset, count);
    if(!response) return std::unexpected(response.error());
    std::vector<bool> values;
    values.reserve(count);
    for(std::uint16_t index = 0; index < count; ++index)
        values.push_back(((*response)[index / 8] & (1u << (index % 8))) != 0);
    return values;
}

std::expected<std::vector<bool>, Failure> Client::ReadDiscreteInputs(
    std::uint8_t slave, std::uint16_t offset, std::uint16_t count)
{
    auto response = Read(slave, 2, offset, count);
    if(!response) return std::unexpected(response.error());
    std::vector<bool> values;
    values.reserve(count);
    for(std::uint16_t index = 0; index < count; ++index)
        values.push_back(((*response)[index / 8] & (1u << (index % 8))) != 0);
    return values;
}

std::expected<std::vector<std::uint16_t>, Failure> Client::ReadHoldingRegisters(
    std::uint8_t slave, std::uint16_t offset, std::uint16_t count)
{
    auto response = Read(slave, 3, offset, count);
    if(!response) return std::unexpected(response.error());
    std::vector<std::uint16_t> values;
    values.reserve(count);
    for(std::size_t index = 0; index < response->size(); index += 2)
        values.push_back(static_cast<std::uint16_t>((*response)[index] << 8 | (*response)[index + 1]));
    return values;
}

std::expected<std::vector<std::uint16_t>, Failure> Client::ReadInputRegisters(
    std::uint8_t slave, std::uint16_t offset, std::uint16_t count)
{
    auto response = Read(slave, 4, offset, count);
    if(!response) return std::unexpected(response.error());
    std::vector<std::uint16_t> values;
    values.reserve(count);
    for(std::size_t index = 0; index < response->size(); index += 2)
        values.push_back(static_cast<std::uint16_t>((*response)[index] << 8 | (*response)[index + 1]));
    return values;
}

std::expected<std::vector<std::uint8_t>, Failure> Client::Read(
    std::uint8_t slave, std::uint8_t function, std::uint16_t offset, std::uint16_t count)
{
    if(count == 0)
        return std::unexpected(Failure{Error::Malformed, 0});

    std::vector<std::uint8_t> request{slave, function};
    AppendU16(request, offset);
    AppendU16(request, count);
    AppendCrc(request);

    Failure last_failure{Error::Timeout, 0};
    for(unsigned attempt = 0; attempt <= m_retries; ++attempt)
    {
        auto response = m_transport.Transact(request, m_timeout);
        if(!response)
        {
            last_failure = {Error::Timeout, 0};
            continue;
        }
        if(response->size() < 5)
        {
            last_failure = {Error::Malformed, 0};
            continue;
        }

        const auto wire_crc = static_cast<std::uint16_t>((*response)[response->size() - 2]) |
                              (static_cast<std::uint16_t>((*response)[response->size() - 1]) << 8);
        if(can_codec::ModbusCrc(std::span<const std::uint8_t>(*response).first(response->size() - 2)) != wire_crc)
        {
            last_failure = {Error::Crc, 0};
            continue;
        }
        if((*response)[0] != slave)
        {
            last_failure = {Error::WrongSlave, 0};
            continue;
        }
        if((*response)[1] == static_cast<std::uint8_t>(function | 0x80))
            return std::unexpected(Failure{Error::Exception, (*response)[2]});
        if((*response)[1] != function)
        {
            last_failure = {Error::WrongFunction, 0};
            continue;
        }

        const std::size_t byte_count = (*response)[2];
        const std::size_t required = function <= 2 ? (count + 7u) / 8u : static_cast<std::size_t>(count) * 2u;
        if(byte_count != required || response->size() != byte_count + 5)
        {
            last_failure = {Error::Malformed, 0};
            continue;
        }
        return std::vector<std::uint8_t>(response->begin() + 3, response->end() - 2);
    }
    return std::unexpected(last_failure);
}
}
