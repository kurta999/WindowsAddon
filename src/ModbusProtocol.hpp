#pragma once

#include <cstdint>
#include <optional>
#include <vector>

namespace modbus
{
enum class Transport : uint8_t { Rtu, Tcp };

enum class ProtocolError : uint8_t
{
    None,
    InvalidRequestRange,
    FrameTooShort,
    Crc,
    MbapLength,
    WrongTransactionId,
    WrongUnitId,
    WrongFunction,
    ExceptionResponse,
    ByteCountMismatch,
    UnexpectedResponse,
};

struct FrameResult
{
    ProtocolError error = ProtocolError::None;
    std::vector<uint8_t> frame;
    bool Ok() const { return error == ProtocolError::None; }
};

struct RegisterParseResult
{
    RegisterParseResult() = default;
    RegisterParseResult(ProtocolError value, uint8_t exception = 0)
        : error(value), exception_code(exception) {}

    ProtocolError error = ProtocolError::None;
    uint8_t exception_code = 0;
    std::vector<uint16_t> registers;
    bool Ok() const { return error == ProtocolError::None; }
};

struct BitParseResult
{
    BitParseResult() = default;
    BitParseResult(ProtocolError value, uint8_t exception = 0)
        : error(value), exception_code(exception) {}

    ProtocolError error = ProtocolError::None;
    uint8_t exception_code = 0;
    std::vector<uint8_t> packed_bits;
    bool Ok() const { return error == ProtocolError::None; }
};

struct WriteParseResult
{
    WriteParseResult() = default;
    WriteParseResult(ProtocolError value, uint8_t exception = 0)
        : error(value), exception_code(exception) {}

    ProtocolError error = ProtocolError::None;
    uint8_t exception_code = 0;
    bool Ok() const { return error == ProtocolError::None; }
};

inline void AppendUint16(std::vector<uint8_t>& frame, uint16_t value)
{
    frame.push_back(static_cast<uint8_t>((value >> 8) & 0xFF));
    frame.push_back(static_cast<uint8_t>(value & 0xFF));
}

inline uint16_t Crc16(std::vector<uint8_t>::const_iterator begin, std::vector<uint8_t>::const_iterator end)
{
    uint16_t crc = 0xFFFF;
    for(auto it = begin; it != end; ++it)
    {
        crc ^= *it;
        for(int bit = 0; bit < 8; ++bit)
        {
            const bool lsb = (crc & 1) != 0;
            crc >>= 1;
            if(lsb)
                crc ^= 0xA001;
        }
    }
    return crc;
}

inline void AppendCrc(std::vector<uint8_t>& frame)
{
    const uint16_t crc = Crc16(frame.begin(), frame.end());
    frame.push_back(static_cast<uint8_t>(crc & 0xFF));
    frame.push_back(static_cast<uint8_t>((crc >> 8) & 0xFF));
}

inline bool IsValidReadRange(uint16_t offset, uint16_t count, uint16_t max_count = 125)
{
    return count != 0 && count <= max_count &&
        static_cast<uint32_t>(offset) + static_cast<uint32_t>(count) <= 0x10000;
}

inline FrameResult BuildReadRequest(Transport transport, uint16_t transaction_id, uint8_t unit_id,
    uint8_t function_code, uint16_t offset, uint16_t count)
{
    if(!IsValidReadRange(offset, count))
        return { ProtocolError::InvalidRequestRange, {} };

    std::vector<uint8_t> frame;
    if(transport == Transport::Tcp)
    {
        AppendUint16(frame, transaction_id);
        AppendUint16(frame, 0);
        AppendUint16(frame, 6);
    }
    frame.push_back(unit_id);
    frame.push_back(function_code);
    AppendUint16(frame, offset);
    AppendUint16(frame, count);
    if(transport == Transport::Rtu)
        AppendCrc(frame);
    return { ProtocolError::None, std::move(frame) };
}

inline RegisterParseResult ParseReadRegistersResponse(Transport transport, const std::vector<uint8_t>& frame,
    uint8_t expected_unit_id, uint8_t expected_function_code, uint16_t expected_count,
    bool rtu_frame_has_crc = true, std::optional<uint16_t> expected_transaction_id = std::nullopt)
{
    size_t base = 0;
    size_t end = frame.size();
    if(transport == Transport::Tcp)
    {
        if(frame.size() < 9)
            return { ProtocolError::FrameTooShort };
        const uint16_t protocol_id = static_cast<uint16_t>((frame[2] << 8) | frame[3]);
        const uint16_t length = static_cast<uint16_t>((frame[4] << 8) | frame[5]);
        if(protocol_id != 0 || length != frame.size() - 6)
            return { ProtocolError::MbapLength };
        const uint16_t transaction_id = static_cast<uint16_t>((frame[0] << 8) | frame[1]);
        if(expected_transaction_id && transaction_id != *expected_transaction_id)
            return { ProtocolError::WrongTransactionId };
        base = 6;
    }
    else if(rtu_frame_has_crc)
    {
        if(frame.size() < 5)
            return { ProtocolError::FrameTooShort };
        const uint16_t expected_crc = static_cast<uint16_t>((frame[end - 1] << 8) | frame[end - 2]);
        if(Crc16(frame.begin(), frame.end() - 2) != expected_crc)
            return { ProtocolError::Crc };
        end -= 2;
    }
    else if(frame.size() < 3)
        return { ProtocolError::FrameTooShort };

    if(base + 2 >= end)
        return { ProtocolError::FrameTooShort };
    if(frame[base] != expected_unit_id)
        return { ProtocolError::WrongUnitId };

    const uint8_t function_code = frame[base + 1];
    if(function_code == (expected_function_code | 0x80))
        return { ProtocolError::ExceptionResponse, frame[base + 2] };
    if(function_code != expected_function_code)
        return { ProtocolError::WrongFunction };

    const uint8_t byte_count = frame[base + 2];
    const size_t data_start = base + 3;
    if(byte_count % 2 != 0 || data_start + byte_count != end ||
        byte_count != static_cast<size_t>(expected_count) * 2)
        return { ProtocolError::ByteCountMismatch };

    RegisterParseResult result;
    result.registers.reserve(byte_count / 2);
    for(size_t i = data_start; i < end; i += 2)
        result.registers.push_back(static_cast<uint16_t>((frame[i] << 8) | frame[i + 1]));
    return result;
}

inline BitParseResult ParseReadBitsResponse(Transport transport, const std::vector<uint8_t>& frame,
    uint8_t expected_unit_id, uint8_t expected_function_code, uint16_t expected_count,
    bool rtu_frame_has_crc = true, std::optional<uint16_t> expected_transaction_id = std::nullopt)
{
    size_t base = 0;
    size_t end = frame.size();
    if(transport == Transport::Tcp)
    {
        if(frame.size() < 9)
            return { ProtocolError::FrameTooShort };
        const uint16_t protocol_id = static_cast<uint16_t>((frame[2] << 8) | frame[3]);
        const uint16_t length = static_cast<uint16_t>((frame[4] << 8) | frame[5]);
        if(protocol_id != 0 || length != frame.size() - 6)
            return { ProtocolError::MbapLength };
        const uint16_t transaction_id = static_cast<uint16_t>((frame[0] << 8) | frame[1]);
        if(expected_transaction_id && transaction_id != *expected_transaction_id)
            return { ProtocolError::WrongTransactionId };
        base = 6;
    }
    else if(rtu_frame_has_crc)
    {
        if(frame.size() < 5)
            return { ProtocolError::FrameTooShort };
        const uint16_t expected_crc = static_cast<uint16_t>((frame[end - 1] << 8) | frame[end - 2]);
        if(Crc16(frame.begin(), frame.end() - 2) != expected_crc)
            return { ProtocolError::Crc };
        end -= 2;
    }
    else if(frame.size() < 3)
        return { ProtocolError::FrameTooShort };

    if(base + 2 >= end)
        return { ProtocolError::FrameTooShort };
    if(frame[base] != expected_unit_id)
        return { ProtocolError::WrongUnitId };
    const uint8_t function_code = frame[base + 1];
    if(function_code == (expected_function_code | 0x80))
        return { ProtocolError::ExceptionResponse, frame[base + 2] };
    if(function_code != expected_function_code)
        return { ProtocolError::WrongFunction };

    const uint8_t byte_count = frame[base + 2];
    const size_t expected_bytes = (static_cast<size_t>(expected_count) + 7) / 8;
    const size_t data_start = base + 3;
    if(byte_count != expected_bytes || data_start + byte_count != end)
        return { ProtocolError::ByteCountMismatch };

    BitParseResult result;
    result.packed_bits.assign(frame.begin() + data_start, frame.begin() + end);
    return result;
}

inline WriteParseResult ParseWriteResponse(Transport transport, const std::vector<uint8_t>& frame,
    uint8_t expected_unit_id, uint8_t expected_function_code, uint16_t expected_offset,
    uint16_t expected_value_or_count, bool rtu_frame_has_crc = true,
    std::optional<uint16_t> expected_transaction_id = std::nullopt)
{
    size_t base = 0;
    size_t end = frame.size();
    if(transport == Transport::Tcp)
    {
        if(frame.size() < 9)
            return { ProtocolError::FrameTooShort };
        const uint16_t protocol_id = static_cast<uint16_t>((frame[2] << 8) | frame[3]);
        const uint16_t length = static_cast<uint16_t>((frame[4] << 8) | frame[5]);
        if(protocol_id != 0 || length != frame.size() - 6)
            return { ProtocolError::MbapLength };
        const uint16_t transaction_id = static_cast<uint16_t>((frame[0] << 8) | frame[1]);
        if(expected_transaction_id && transaction_id != *expected_transaction_id)
            return { ProtocolError::WrongTransactionId };
        base = 6;
    }
    else if(rtu_frame_has_crc)
    {
        if(frame.size() < 5)
            return { ProtocolError::FrameTooShort };
        const uint16_t expected_crc = static_cast<uint16_t>((frame[end - 1] << 8) | frame[end - 2]);
        if(Crc16(frame.begin(), frame.end() - 2) != expected_crc)
            return { ProtocolError::Crc };
        end -= 2;
    }
    else if(frame.size() < 3)
        return { ProtocolError::FrameTooShort };

    if(base + 2 >= end)
        return { ProtocolError::FrameTooShort };
    if(frame[base] != expected_unit_id)
        return { ProtocolError::WrongUnitId };
    const uint8_t function_code = frame[base + 1];
    if(function_code == (expected_function_code | 0x80))
        return { ProtocolError::ExceptionResponse, frame[base + 2] };
    if(function_code != expected_function_code)
        return { ProtocolError::WrongFunction };
    if(end - base != 6)
        return { ProtocolError::ByteCountMismatch };

    const uint16_t offset = static_cast<uint16_t>((frame[base + 2] << 8) | frame[base + 3]);
    const uint16_t value_or_count = static_cast<uint16_t>((frame[base + 4] << 8) | frame[base + 5]);
    if(offset != expected_offset || value_or_count != expected_value_or_count)
        return { ProtocolError::UnexpectedResponse };
    return {};
}
}
