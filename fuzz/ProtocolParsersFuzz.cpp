#include "CanCodecs.hpp"
#include "ModbusClient.hpp"
#include "SensorDataParser.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

extern "C"
{
#include <isotp/isotp.h>
}

namespace
{
class FuzzTransport : public modbus::ITransport
{
public:
    explicit FuzzTransport(std::span<const std::uint8_t> response)
        : m_response(response.begin(), response.end()) {}

    std::optional<std::vector<std::uint8_t>> Transact(
        std::span<const std::uint8_t>, std::chrono::milliseconds) override
    {
        return m_response;
    }

private:
    std::vector<std::uint8_t> m_response;
};
}

extern "C" void isotp_user_debug(const char*, ...)
{
}

extern "C" int isotp_user_send_can(uint32_t, const uint8_t*, uint8_t)
{
    return ISOTP_RET_OK;
}

extern "C" uint32_t isotp_user_get_ms(void)
{
    return 0;
}

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size)
{
    const std::span<const std::uint8_t> bytes(data, size);

    can_codec::Stm32StreamDecoder stm32;
    (void)stm32.Feed(bytes);

    can_codec::LawicelStreamDecoder lawicel;
    (void)lawicel.Feed(std::string_view(reinterpret_cast<const char*>(data), size));

    (void)can_codec::ModbusCrc(bytes);
    (void)SensorDataParser::Parse(reinterpret_cast<const char*>(data), size);

    FuzzTransport transport(bytes);
    modbus::Client modbus_client(transport);
    const auto count = static_cast<std::uint16_t>(1 + (size == 0 ? 0 : data[0] % 125));
    (void)modbus_client.ReadCoils(1, 0, count);
    (void)modbus_client.ReadDiscreteInputs(1, 0, count);
    (void)modbus_client.ReadHoldingRegisters(1, 0, count);
    (void)modbus_client.ReadInputRegisters(1, 0, count);

    std::array<std::uint8_t, 256> send_buffer{};
    std::array<std::uint8_t, 256> receive_buffer{};
    IsoTpLink link{};
    isotp_init_link(&link, 0x700, send_buffer.data(), static_cast<std::uint16_t>(send_buffer.size()),
                    receive_buffer.data(), static_cast<std::uint16_t>(receive_buffer.size()));

    std::array<std::uint8_t, 8> can_frame{};
    const auto can_size = std::min(size, can_frame.size());
    if(can_size != 0)
        std::copy_n(data, can_size, can_frame.begin());
    isotp_on_can_message(&link, can_frame.data(), static_cast<std::uint8_t>(can_size));
    isotp_poll(&link);

    std::array<std::uint8_t, 256> payload{};
    const auto payload_size = std::min(size, payload.size());
    if(payload_size != 0)
        std::copy_n(data, payload_size, payload.begin());
    (void)isotp_send(&link, payload.data(), static_cast<std::uint16_t>(payload_size));

    std::array<std::uint8_t, 256> output{};
    std::uint16_t output_size = 0;
    (void)isotp_receive(&link, output.data(), static_cast<std::uint16_t>(output.size()), &output_size);
    return 0;
}
