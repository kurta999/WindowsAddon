#pragma once

#include <array>
#include <cstdint>
#include <format>
#include <optional>
#include <span>
#include <string>

// !\brief The special-register block a PGL device publishes, decoded.
//
// The layout knowledge lived inside ModbusSpecialRegisterPanel::AppendLog,
// between two wxGrid calls: registers 0..3 pack a timestamp as two 32-bit
// decimal fields (HHMMSS and DDMMYY), register 8 is an error id, and 4..7 are
// that error's parameters. The panel indexed all nine without checking the
// block's length, so a short frame was undefined behaviour rather than a
// refused decode.
namespace modbus_special
{
inline constexpr size_t kRecordRegisters = 9;

struct Record
{
    int year{};
    int month{};
    int day{};
    int hour{};
    int minute{};
    int second{};
    std::uint16_t error_id{};
    std::array<std::uint16_t, 4> params{};
};

// !\brief Decode one block, or nothing when it is too short to hold one.
[[nodiscard]] constexpr std::optional<Record> Decode(std::span<const std::uint16_t> registers)
{
    if(registers.size() < kRecordRegisters)
        return std::nullopt;

    const std::uint32_t time_time = registers[1] | registers[0] << 16;
    const std::uint32_t time_date = registers[3] | registers[2] << 16;

    Record record;
    record.hour = static_cast<int>(time_time / 10000);
    record.minute = static_cast<int>((time_time / 100) % 100);
    record.second = static_cast<int>(time_time % 100);
    record.year = 2000 + static_cast<int>(time_date % 100);
    record.month = static_cast<int>((time_date / 100) % 100);
    record.day = static_cast<int>(time_date / 10000);
    record.error_id = registers[8];
    record.params = { registers[4], registers[5], registers[6], registers[7] };
    return record;
}

// !\brief The line the log grid shows for one record.
[[nodiscard]] inline std::string FormatRecord(const Record& record)
{
    std::string text = std::format("{:04}.{:02}.{:02} {:02}:{:02}:{:02} - ",
        record.year, record.month, record.day, record.hour, record.minute, record.second);
    if(record.error_id == 100)
        text += "SpecialError1";
    else
        text += std::format("Error: {} - {}, {}, {}, {}", record.error_id,
            record.params[0], record.params[1], record.params[2], record.params[3]);
    return text;
}
}
