#include "TestFramework.hpp"

#include "CanCodecs.hpp"
#include "ModbusClient.hpp"

#include <chrono>
#include <cstdint>
#include <optional>
#include <utility>
#include <vector>

namespace
{
void AddCrc(std::vector<std::uint8_t>& frame)
{
    const auto crc = can_codec::ModbusCrc(frame);
    frame.push_back(static_cast<std::uint8_t>(crc));
    frame.push_back(static_cast<std::uint8_t>(crc >> 8));
}

std::vector<std::uint8_t> Response(std::uint8_t function, std::vector<std::uint8_t> data)
{
    std::vector<std::uint8_t> result{0x11, function, static_cast<std::uint8_t>(data.size())};
    result.insert(result.end(), data.begin(), data.end());
    AddCrc(result);
    return result;
}

class FakeTransport final : public modbus::ITransport
{
public:
    std::optional<std::vector<std::uint8_t>> Transact(
        std::span<const std::uint8_t> request, std::chrono::milliseconds timeout) override
    {
        requests.emplace_back(request.begin(), request.end());
        timeouts.push_back(timeout);
        if(responses.empty()) return std::nullopt;
        auto response = std::move(responses.front());
        responses.erase(responses.begin());
        return response;
    }

    std::vector<std::optional<std::vector<std::uint8_t>>> responses;
    std::vector<std::vector<std::uint8_t>> requests;
    std::vector<std::chrono::milliseconds> timeouts;
};
}

TEST_CASE(ModbusFakeTransportCoversCoilsAndDiscreteInputs)
{
    FakeTransport transport;
    transport.responses = {Response(1, {0b00000101}), Response(2, {0b00000010})};
    modbus::Client client(transport);

    const auto coils = client.ReadCoils(0x11, 0x20, 3);
    const auto inputs = client.ReadDiscreteInputs(0x11, 0x30, 2);
    EXPECT_TRUE(coils.has_value());
    EXPECT_TRUE((*coils)[0]);
    EXPECT_FALSE((*coils)[1]);
    EXPECT_TRUE((*coils)[2]);
    EXPECT_FALSE((*inputs)[0]);
    EXPECT_TRUE((*inputs)[1]);
    EXPECT_EQ(transport.requests[0][1], std::uint8_t{1});
    EXPECT_EQ(transport.requests[1][1], std::uint8_t{2});
}

TEST_CASE(ModbusFakeTransportCoversRegistersAndBigEndianWireOrder)
{
    FakeTransport transport;
    transport.responses = {Response(3, {0x12, 0x34, 0xAB, 0xCD}), Response(4, {0x80, 0x01})};
    modbus::Client client(transport);

    const auto holding = client.ReadHoldingRegisters(0x11, 0x1234, 2);
    const auto input = client.ReadInputRegisters(0x11, 0x0102, 1);
    EXPECT_TRUE(holding.has_value());
    EXPECT_EQ((*holding)[0], std::uint16_t{0x1234});
    EXPECT_EQ((*holding)[1], std::uint16_t{0xABCD});
    EXPECT_EQ((*input)[0], std::uint16_t{0x8001});
    EXPECT_EQ(transport.requests[0][2], std::uint8_t{0x12});
    EXPECT_EQ(transport.requests[0][3], std::uint8_t{0x34});
}

TEST_CASE(ModbusReportsExceptionResponses)
{
    FakeTransport transport;
    std::vector<std::uint8_t> exception{0x11, 0x83, 0x02};
    AddCrc(exception);
    transport.responses = {exception};
    modbus::Client client(transport);

    const auto result = client.ReadHoldingRegisters(0x11, 0, 1);
    EXPECT_FALSE(result.has_value());
    EXPECT_EQ(result.error().error, modbus::Error::Exception);
    EXPECT_EQ(result.error().exception_code, std::uint8_t{2});
}

TEST_CASE(ModbusRejectsCrcErrorsAndPartialReads)
{
    FakeTransport crc_transport;
    auto bad_crc = Response(3, {0x00, 0x01});
    bad_crc.back() ^= 0xFF;
    crc_transport.responses = {bad_crc};
    modbus::Client crc_client(crc_transport);
    const auto crc_result = crc_client.ReadHoldingRegisters(0x11, 0, 1);
    EXPECT_EQ(crc_result.error().error, modbus::Error::Crc);

    FakeTransport partial_transport;
    partial_transport.responses = {std::vector<std::uint8_t>{0x11, 0x03, 0x02, 0x12}};
    modbus::Client partial_client(partial_transport);
    const auto partial = partial_client.ReadHoldingRegisters(0x11, 0, 1);
    EXPECT_EQ(partial.error().error, modbus::Error::Malformed);
}

TEST_CASE(ModbusTimeoutUsesConfiguredTimeoutAndRetries)
{
    FakeTransport transport;
    transport.responses = {std::nullopt, std::nullopt, Response(4, {0x00, 0x2A})};
    modbus::Client client(transport);
    client.SetRetries(2);
    client.SetTimeout(std::chrono::milliseconds(37));

    const auto result = client.ReadInputRegisters(0x11, 0, 1);
    EXPECT_TRUE(result.has_value());
    EXPECT_EQ((*result)[0], std::uint16_t{42});
    EXPECT_EQ(transport.requests.size(), size_t{3});
    EXPECT_EQ(transport.timeouts[0].count(), std::int64_t{37});
}

TEST_CASE(ModbusReturnsTimeoutAfterRetryBudgetIsExhausted)
{
    FakeTransport transport;
    transport.responses = {std::nullopt, std::nullopt};
    modbus::Client client(transport);
    client.SetRetries(1);

    const auto result = client.ReadCoils(0x11, 0, 1);
    EXPECT_FALSE(result.has_value());
    EXPECT_EQ(result.error().error, modbus::Error::Timeout);
    EXPECT_EQ(transport.requests.size(), size_t{2});
}
