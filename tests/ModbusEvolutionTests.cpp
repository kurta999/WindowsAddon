#include "TestFramework.hpp"

#include "ModbusConditionalColors.hpp"
#include "ModbusCustomCommand.hpp"
#include "ModbusProtocol.hpp"
#include "ModbusRegisterEditor.hpp"
#include "ModbusRegisterGrouping.hpp"
#include "ModbusRegisterValueCodec.hpp"
#include "PendingModbusRowUpdates.hpp"
#include "SerialPortConnectionStatus.hpp"

#include <cmath>
#include <cstring>
#include <expected>
#include <future>

namespace
{
std::unique_ptr<ModbusItem> Register(size_t offset, ModbusBitfieldType type = MBT_UI16)
{
    auto item = std::make_unique<ModbusItem>("register", uint8_t{0}, offset, type, MVF_DEC, "", 0, 0, 0);
    item->branches = 1;
    return item;
}
}

TEST_CASE(ModbusProtocolBuildsAndValidatesRtuAndTcpFrames)
{
    const auto rtu = modbus::BuildReadRequest(modbus::Transport::Rtu, 0, 0x11, 3, 0x1234, 2);
    EXPECT_TRUE(rtu.Ok());
    EXPECT_EQ(rtu.frame.size(), size_t{8});
    EXPECT_EQ(rtu.frame[0], uint8_t{0x11});
    EXPECT_EQ(rtu.frame[1], uint8_t{3});
    EXPECT_EQ(modbus::Crc16(rtu.frame.begin(), rtu.frame.end() - 2),
        static_cast<uint16_t>((rtu.frame.back() << 8) | rtu.frame[rtu.frame.size() - 2]));

    const auto tcp = modbus::BuildReadRequest(modbus::Transport::Tcp, 0x1234, 7, 4, 10, 1);
    EXPECT_TRUE(tcp.Ok());
    EXPECT_EQ(tcp.frame.size(), size_t{12});
    EXPECT_EQ(tcp.frame[0], uint8_t{0x12});
    EXPECT_EQ(tcp.frame[1], uint8_t{0x34});
    EXPECT_FALSE(modbus::BuildReadRequest(modbus::Transport::Rtu, 0, 1, 3, 0, 0).Ok());
    EXPECT_FALSE(modbus::BuildReadRequest(modbus::Transport::Rtu, 0, 1, 3, 0, 126).Ok());
}

TEST_CASE(ModbusProtocolRejectsWrongIdsExceptionsAndMalformedLengths)
{
    std::vector<uint8_t> valid{1, 3, 4, 0x12, 0x34, 0xAB, 0xCD};
    modbus::AppendCrc(valid);
    const auto parsed = modbus::ParseReadRegistersResponse(modbus::Transport::Rtu, valid, 1, 3, 2);
    EXPECT_TRUE(parsed.Ok());
    EXPECT_EQ(parsed.registers[0], uint16_t{0x1234});
    EXPECT_EQ(parsed.registers[1], uint16_t{0xABCD});

    auto wrong_id = modbus::ParseReadRegistersResponse(modbus::Transport::Rtu, valid, 2, 3, 2);
    EXPECT_EQ(wrong_id.error, modbus::ProtocolError::WrongUnitId);
    std::vector<uint8_t> exception{1, 0x83, 2};
    modbus::AppendCrc(exception);
    EXPECT_EQ(modbus::ParseReadRegistersResponse(modbus::Transport::Rtu, exception, 1, 3, 2).error,
        modbus::ProtocolError::ExceptionResponse);
    valid[2] = 3;
    EXPECT_FALSE(modbus::ParseReadRegistersResponse(modbus::Transport::Rtu, valid, 1, 3, 2).Ok());

    const std::vector<uint8_t> tcp{0x12, 0x34, 0, 0, 0, 5, 7, 4, 2, 0, 1};
    EXPECT_EQ(modbus::ParseReadRegistersResponse(modbus::Transport::Tcp, tcp, 7, 4, 1, true,
        uint16_t{0x9999}).error, modbus::ProtocolError::WrongTransactionId);
}

TEST_CASE(ModbusRegisterGroupingSkipsGapsAndMergesOverlaps)
{
    ModbusItemType items;
    items.push_back(Register(10, MBT_UI32));
    items.push_back(Register(0));
    items.push_back(Register(1));
    items.push_back(Register(11));
    items.push_back(Register(30));
    const auto groups = BuildContiguousModbusRegisterRanges(items, 1);
    ASSERT_EQ(groups.size(), size_t{3});
    EXPECT_EQ(groups[0].offset, size_t{0});
    EXPECT_EQ(groups[0].count, size_t{2});
    EXPECT_EQ(groups[1].offset, size_t{10});
    EXPECT_EQ(groups[1].count, size_t{2});
    EXPECT_EQ(groups[2].offset, size_t{30});
}

TEST_CASE(ModbusGroupedReadsPreserveRegisterOffsets)
{
    ModbusItemType items;
    items.push_back(Register(2));
    items.push_back(Register(3));
    items.push_back(Register(8));
    std::vector<std::pair<uint16_t, uint16_t>> calls;
    const auto result = ReadGroupedModbusRegisters(items, 1,
        [&](uint16_t offset, uint16_t count) -> std::expected<std::vector<uint16_t>, uint8_t>
        {
            calls.push_back({offset, count});
            std::vector<uint16_t> values;
            for(uint16_t i = 0; i < count; ++i)
                values.push_back(static_cast<uint16_t>(offset + i));
            return values;
        }, 100);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(calls.size(), size_t{2});
    EXPECT_EQ(calls[0].first, uint16_t{102});
    EXPECT_EQ(result->values.at(8), uint16_t{108});
}

TEST_CASE(ModbusValueCodecRoundTripsAllWordOrders)
{
    constexpr uint64_t raw = UINT64_C(0x0123456789ABCDEF);
    for(const auto order : { ModbusRegisterByteOrder::BigEndian, ModbusRegisterByteOrder::LittleEndian,
        ModbusRegisterByteOrder::BigEndianByteSwap, ModbusRegisterByteOrder::LittleEndianByteSwap })
    {
        const auto words = EncodeModbusRegister64(raw, order);
        EXPECT_EQ(DecodeModbusRegister64(words[0], words[1], words[2], words[3], order), raw);
    }

    const double expected = 1234.5678;
    uint64_t bits = 0;
    std::memcpy(&bits, &expected, sizeof(bits));
    const auto words = EncodeModbusRegister64(bits, ModbusRegisterByteOrder::LittleEndianByteSwap);
    const double decoded = DecodeModbusRegisterDouble(words[0], words[1], words[2], words[3],
        ModbusRegisterByteOrder::LittleEndianByteSwap);
    EXPECT_TRUE(std::fabs(decoded - expected) < 0.0000001);
}

TEST_CASE(ModbusRegisterEditorReflowsOnlyTheSelectedContiguousGroup)
{
    ModbusItemType items;
    items.push_back(Register(0));
    items.push_back(Register(1, MBT_UI32));
    items.push_back(Register(10));
    auto result = EditModbusRegisterLayout(items, 0, ModbusRegisterEditAction::InsertAfter);
    EXPECT_TRUE(result.success);
    EXPECT_EQ(items[0]->m_Offset, size_t{0});
    EXPECT_EQ(items[1]->m_Offset, size_t{1});
    EXPECT_EQ(items[2]->m_Offset, size_t{2});
    EXPECT_EQ(items[3]->m_Offset, size_t{10});

    result = ChangeModbusRegisterType(items, 1, MBT_DOUBLE);
    EXPECT_TRUE(result.success);
    EXPECT_EQ(items[2]->m_Offset, size_t{5});
}

TEST_CASE(ModbusScalingFeedsConditionalColors)
{
    auto item = Register(0);
    item->m_Value.SetInteger(500);
    item->m_ValueScaling = { true, 0, 0, 1000, 100, 1 };
    item->m_ConditionalColors[0].comparison = ModbusConditionalColorComparison::GreaterThanOrEqualTo;
    item->m_ConditionalColors[0].value = 50;
    item->m_ConditionalColors[0].color = 0xFF0000;
    EXPECT_TRUE(std::fabs(GetModbusItemDisplayNumericValue(*item) - 50.0) < 0.0001);
    EXPECT_TRUE(FindMatchingModbusConditionalColorRule(*item) != nullptr);
}

TEST_CASE(ModbusCustomCommandParsesCompactHexAndChecksums)
{
    auto parsed = modbus_custom_command::ParseHexBytes("01 03 0000,0002");
    ASSERT_TRUE(parsed.has_value());
    EXPECT_EQ(parsed->size(), size_t{6});
    modbus_custom_command::AppendCheck(*parsed, modbus_custom_command::CheckType::Crc);
    EXPECT_EQ(parsed->size(), size_t{8});
    EXPECT_EQ(modbus_custom_command::FormatHexBytes(*parsed).substr(0, 5), std::string("01 03"));
    EXPECT_FALSE(modbus_custom_command::ParseHexBytes("0xGG").has_value());
}

TEST_CASE(ModbusProtocolValidatesWriteEchoes)
{
    std::vector<uint8_t> response{1, 6, 0, 10, 0x12, 0x34};
    modbus::AppendCrc(response);
    EXPECT_TRUE(modbus::ParseWriteResponse(modbus::Transport::Rtu, response, 1, 6, 10, 0x1234).Ok());
    EXPECT_EQ(modbus::ParseWriteResponse(modbus::Transport::Rtu, response, 1, 6, 11, 0x1234).error,
        modbus::ProtocolError::UnexpectedResponse);
}

TEST_CASE(PendingModbusRowsAreDeduplicatedAndDrained)
{
    PendingModbusRowUpdates pending;
    pending.Add({3, 1, 3});
    const auto rows = pending.Take();
    ASSERT_EQ(rows.size(), size_t{2});
    EXPECT_EQ(rows[0], uint8_t{1});
    EXPECT_EQ(rows[1], uint8_t{3});
    EXPECT_TRUE(pending.Take().empty());
}

TEST_CASE(SerialConnectionStatusUsesCacheWhileTransportIsBusy)
{
    std::mutex transport_mutex;
    SerialPortConnectionStatus cached;
    cached.Store(SerialPortConnectionState::Connecting);
    std::promise<void> locked;
    std::promise<void> release;
    const auto release_future = release.get_future().share();
    std::thread owner([&]
    {
        std::scoped_lock lock(transport_mutex);
        locked.set_value();
        release_future.wait();
    });
    locked.get_future().wait();
    bool probed = false;
    const auto result = ProbeSerialPortConnectionStatusNonBlocking(transport_mutex, cached, [&]
    {
        probed = true;
        return SerialPortConnectionState::Connected;
    });
    release.set_value();
    owner.join();
    EXPECT_EQ(result, SerialPortConnectionState::Connecting);
    EXPECT_FALSE(probed);
}
