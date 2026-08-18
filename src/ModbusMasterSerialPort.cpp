#include "pch.hpp"
#include "ModbusProtocol.hpp"

constexpr uint32_t RX_QUEUE_MAX_SIZE = 1000;

constexpr auto SERIAL_PORT_TIMEOUT           = 100ms;
constexpr auto SERIAL_PORT_EXCEPTION_TIMEOUT = 1000ms;

constexpr size_t MAX_HOLDING_REG_ONCE = 120;

namespace
{
ModbusError ToModbusError(modbus::ProtocolError error)
{
    switch(error)
    {
        case modbus::ProtocolError::InvalidRequestRange: return ModbusError::InvalidRequest;
        case modbus::ProtocolError::Crc: return ModbusError::Crc;
        case modbus::ProtocolError::FrameTooShort:
        case modbus::ProtocolError::MbapLength:
        case modbus::ProtocolError::ByteCountMismatch: return ModbusError::InvalidLength;
        case modbus::ProtocolError::WrongUnitId: return ModbusError::WrongId;
        case modbus::ProtocolError::WrongTransactionId: return ModbusError::WrongTransactionId;
        case modbus::ProtocolError::ExceptionResponse: return ModbusError::ExceptionResponse;
        case modbus::ProtocolError::WrongFunction:
        case modbus::ProtocolError::UnexpectedResponse:
        case modbus::ProtocolError::None: return ModbusError::UnexpectedResponse;
    }
    return ModbusError::UnexpectedResponse;
}

modbus::Transport ToTransport(bool tcp)
{
    return tcp ? modbus::Transport::Tcp : modbus::Transport::Rtu;
}
}

template <typename In> inline void WriteToByteBuffer(std::vector<uint8_t>& vec, const In& data)
{
    using T = std::decay_t<decltype(data)>;
    if constexpr(is_any<T, uint8_t, int8_t>)
    {
        vec.push_back(data);
    }
    else if constexpr(is_any<T, uint16_t, int16_t>)
    {
        vec.push_back(data >> 8 & 0xFF);
        vec.push_back(data & 0xFF);
    }
    else
        static_assert(always_false_v<T>, "bad type - WriteToByteBuffer!");
}

ModbusMasterSerialPort::ModbusMasterSerialPort() = default;

ModbusMasterSerialPort::~ModbusMasterSerialPort()
{
    std::unique_lock lock{ m_RecvMutex };
    m_RecvData.push_back(10);
    m_RecvCv.notify_all();
}

uint8_t ModbusMasterSerialPort::ExtractFunctionCode(const std::vector<uint8_t>& data) const
{
    if (!IsTcp()) return data.size() > 1 ? data[1] : 0xFF;
    return data.size() > 7 ? data[7] : 0xFF;
}

size_t ModbusMasterSerialPort::TrimmedLen(size_t raw_len) const
{
    return (IsTcp() && raw_len > 6) ? raw_len - 6 : raw_len;
}

bool ModbusMasterSerialPort::WaitForResponse()
{
    std::unique_lock lock{ m_RecvMutex };
    return m_RecvCv.wait_for(lock, *m_stopToken, std::chrono::milliseconds(m_ResponseTimeout),
        [this]() { return m_RecvData.size() > 0; });
}

ModbusMasterSerialPort::ResponseStatus ModbusMasterSerialPort::NotifyAndWaitForResponse(const std::vector<uint8_t>& vec)
{
    {
        std::scoped_lock lock(m_RecvMutex);
        m_RawRecvData.clear();
        m_RecvData.clear();
        m_LastDataCrcOk = true;
    }
    m_SentData = vec;
    NotifiyMainThread();

    bool cv_ret = WaitForResponse();
    ResponseStatus ret = ResponseStatus::Timeout;

    if(cv_ret)
    {
        if(IsTcp())
        {
            m_LastDataCrcOk = m_RecvData.size() >= 6;
            if(m_LastDataCrcOk)
            {
                const uint16_t length = static_cast<uint16_t>((m_RecvData[4] << 8) | m_RecvData[5]);
                m_LastDataCrcOk = length == m_RecvData.size() - 6;
            }
        }

        if (m_recorder && m_recorder->IsRecording())
        {
            ModbusErrorType error_type = m_LastDataCrcOk ? ModbusErrorType::MB_ERR_OK : ModbusErrorType::MB_ERR_CRC;
            m_recorder->RecordFrame(MODBUS_LOG_DIR_RX, ExtractFunctionCode(m_RecvData),
                error_type, m_RecvData.data(), TrimmedLen(m_RecvData.size()));
        }

        timeout_packets = 0;
        if(!m_LastDataCrcOk)
        {
            ret = ResponseStatus::CrcError;
        }
        else
        {
            ret = ResponseStatus::Ok;
            const uint8_t function_code = ExtractFunctionCode(m_RecvData);
            if((function_code & 0x80) != 0)
            {
                modbusErrorCount[function_code]++;
                ret = ResponseStatus::ModbusException;
            }
        }
    }
    else
    {
        if (!m_recorder || !m_recorder->IsReady()) { timeout_packets++; return ret; }
        if (m_recorder->IsRecording())
        {
            uint8_t fc = ExtractFunctionCode(m_LastSentData);
            m_recorder->RecordFrame(MODBUS_LOG_DIR_RX, fc, ModbusErrorType::MB_ERR_TIMEOUT,
                m_LastSentData.data(), m_LastSentData.size());
        }
        timeout_packets++;
    }
    return ret;
}

void ModbusMasterSerialPort::AddCrcToFrame(std::vector<uint8_t>& vec)
{
    if (!IsTcp())
    {
        modbus::AppendCrc(vec);
    }
}

void ModbusMasterSerialPort::SetupHeader(std::vector<uint8_t>& vec, uint8_t slave_id, uint16_t fcode, uint16_t len)
{
    if (IsTcp())
    {
        WriteToByteBuffer<uint16_t>(vec, sequence_id);
        WriteToByteBuffer<uint16_t>(vec, 0x0);
        WriteToByteBuffer<uint16_t>(vec, len + 2);
        WriteToByteBuffer<uint8_t>(vec, slave_id);
        WriteToByteBuffer<uint8_t>(vec, fcode);
        sequence_id++;
    }
    else
    {
        WriteToByteBuffer<uint8_t>(vec, static_cast<uint8_t>(slave_id));
        WriteToByteBuffer<uint8_t>(vec, static_cast<uint8_t>(fcode));
    }
}

void ModbusMasterSerialPort::DoCleanup(std::vector<uint8_t>& recv_data)
{
    if (IsTcp() && recv_data.size() > 3)
    {
        recv_data.erase(recv_data.begin(), recv_data.begin() + 3);
        recv_data.erase(recv_data.begin() + 2, recv_data.begin() + 5);
    }
}

std::expected<std::vector<uint8_t>, ModbusError> ModbusMasterSerialPort::ReadCoilStatus(uint8_t slave_id, uint16_t read_offset, uint16_t read_count)
{
    const std::scoped_lock request_lock(m_RequestMutex);
    const auto frame = modbus::BuildReadRequest(ToTransport(IsTcp()), sequence_id, slave_id,
        FC_ReadCoilStatus, read_offset, read_count);
    if(!frame.Ok())
        return std::unexpected(ToModbusError(frame.error));
    if(IsTcp())
        ++sequence_id;
    const auto response = NotifyAndWaitForResponse(frame.frame);
    if(response != ResponseStatus::Ok)
    {
        m_RecvData.clear();
        return std::unexpected(response == ResponseStatus::ModbusException ? ModbusError::ExceptionResponse :
            response == ResponseStatus::CrcError ? ModbusError::Crc : ModbusError::UnexpectedResponse);
    }
    const auto parsed = modbus::ParseReadBitsResponse(ToTransport(IsTcp()), m_RecvData, slave_id,
        FC_ReadCoilStatus, read_count, false, IsTcp() ? std::optional<uint16_t>(
            static_cast<uint16_t>((frame.frame[0] << 8) | frame.frame[1])) : std::nullopt);
    m_RecvData.clear();
    if(!parsed.Ok())
        return std::unexpected(ToModbusError(parsed.error));
    return parsed.packed_bits;
}

std::expected<std::vector<uint8_t>, ModbusError> ModbusMasterSerialPort::ForceSingleCoil(uint8_t slave_id, uint16_t write_offset, bool status)
{
    const std::scoped_lock request_lock(m_RequestMutex);
    std::vector<uint8_t> vec;
    SetupHeader(vec, slave_id, FC_ForceSingleCoil, 4);
    WriteToByteBuffer(vec, write_offset);
    vec.push_back(status ? 0xFF : 0x0);
    vec.push_back(0x0);
    AddCrcToFrame(vec);

    auto response = NotifyAndWaitForResponse(vec);
    if(response != ResponseStatus::Ok)
    {
        m_RecvData.clear();
        return std::unexpected(response == ResponseStatus::ModbusException ? ModbusError::ExceptionResponse :
            response == ResponseStatus::CrcError ? ModbusError::Crc : ModbusError::UnexpectedResponse);
    }
    const auto parsed = modbus::ParseWriteResponse(ToTransport(IsTcp()), m_RecvData, slave_id,
        FC_ForceSingleCoil, write_offset, status ? uint16_t{0xFF00} : uint16_t{0}, false,
        IsTcp() ? std::optional<uint16_t>(static_cast<uint16_t>((vec[0] << 8) | vec[1])) : std::nullopt);
    m_RecvData.clear();
    if(!parsed.Ok())
        return std::unexpected(ToModbusError(parsed.error));
    return vec;
}

std::expected<std::vector<uint8_t>, ModbusError> ModbusMasterSerialPort::ReadInputStatus(uint8_t slave_id, uint16_t read_offset, uint16_t read_count)
{
    const std::scoped_lock request_lock(m_RequestMutex);
    const auto frame = modbus::BuildReadRequest(ToTransport(IsTcp()), sequence_id, slave_id,
        FC_ReadInputStatus, read_offset, read_count);
    if(!frame.Ok())
        return std::unexpected(ToModbusError(frame.error));
    if(IsTcp())
        ++sequence_id;
    const auto response = NotifyAndWaitForResponse(frame.frame);
    if(response != ResponseStatus::Ok)
    {
        m_RecvData.clear();
        return std::unexpected(response == ResponseStatus::ModbusException ? ModbusError::ExceptionResponse :
            response == ResponseStatus::CrcError ? ModbusError::Crc : ModbusError::UnexpectedResponse);
    }
    const auto parsed = modbus::ParseReadBitsResponse(ToTransport(IsTcp()), m_RecvData, slave_id,
        FC_ReadInputStatus, read_count, false, IsTcp() ? std::optional<uint16_t>(
            static_cast<uint16_t>((frame.frame[0] << 8) | frame.frame[1])) : std::nullopt);
    m_RecvData.clear();
    if(!parsed.Ok())
        return std::unexpected(ToModbusError(parsed.error));
    return parsed.packed_bits;
}

std::expected<std::vector<uint16_t>, ModbusError> ModbusMasterSerialPort::ReadHoldingRegister(uint8_t slave_id, uint16_t read_offset, uint16_t read_count)
{
    const std::scoped_lock request_lock(m_RequestMutex);
    const auto frame = modbus::BuildReadRequest(ToTransport(IsTcp()), sequence_id, slave_id,
        FC_ReadHoldingRegister, read_offset, read_count);
    if(!frame.Ok())
        return std::unexpected(ToModbusError(frame.error));
    if(IsTcp())
        ++sequence_id;
    const auto response = NotifyAndWaitForResponse(frame.frame);
    if(response != ResponseStatus::Ok)
    {
        m_RecvData.clear();
        return std::unexpected(response == ResponseStatus::ModbusException ? ModbusError::ExceptionResponse :
            response == ResponseStatus::CrcError ? ModbusError::Crc : ModbusError::UnexpectedResponse);
    }
    const auto parsed = modbus::ParseReadRegistersResponse(ToTransport(IsTcp()), m_RecvData, slave_id,
        FC_ReadHoldingRegister, read_count, false, IsTcp() ? std::optional<uint16_t>(
            static_cast<uint16_t>((frame.frame[0] << 8) | frame.frame[1])) : std::nullopt);
    m_RecvData.clear();
    if(!parsed.Ok())
        return std::unexpected(ToModbusError(parsed.error));
    return parsed.registers;
}

std::expected<std::vector<uint16_t>, ModbusError> ModbusMasterSerialPort::ReadHoldingRegisters(uint8_t slave_id, uint16_t read_offset, uint16_t read_count)
{
    if(read_count == 0 || static_cast<uint32_t>(read_offset) + read_count > 0x10000)
        return std::unexpected(ModbusError::InvalidRequest);
    std::vector<uint16_t> result;
    size_t remaining = read_count;
    uint16_t offset = read_offset;
    while (remaining > 0)
    {
        size_t step = std::min<size_t>(remaining, MAX_HOLDING_REG_ONCE);
        auto tmp = ReadHoldingRegister(slave_id, offset, step);
        if(!tmp.has_value())
            return std::unexpected(tmp.error());
        if(tmp->size() != step)
            return std::unexpected(ModbusError::InvalidLength);

        result.insert(result.end(), tmp->begin(), tmp->end());
        offset    += step;
        remaining -= step;
    }
    return result;
}

std::expected<std::vector<uint8_t>, ModbusError> ModbusMasterSerialPort::WriteHoldingRegister(uint8_t slave_id, uint16_t write_offset, uint16_t write_count, std::vector<uint16_t> buffer)
{
    const std::scoped_lock request_lock(m_RequestMutex);
    if(write_count == 0 || write_count != buffer.size() || write_count > 123 ||
        static_cast<uint32_t>(write_offset) + write_count > 0x10000)
        return std::unexpected(ModbusError::InvalidRequest);
    std::vector<uint8_t> vec;
    if(write_count == 1)
    {
        SetupHeader(vec, slave_id, FC_WriteSingleRegister, 4);
        vec.push_back(write_offset >> 8 & 0xFF);
        vec.push_back(write_offset & 0xFF);
        vec.push_back(buffer[0] >> 8 & 0xFF);
        vec.push_back(buffer[0] & 0xFF);
    }
    else
    {
        size_t buffer_size = buffer.size() * 2;
        SetupHeader(vec, slave_id, FC_WriteMultipleRegister, 5 + static_cast<uint16_t>(buffer_size));
        WriteToByteBuffer(vec, write_offset);
        WriteToByteBuffer(vec, write_count);
        vec.push_back(buffer_size);
        for(auto& reg : buffer) { vec.push_back(reg >> 8 & 0xFF); vec.push_back(reg & 0xFF); }
    }
    AddCrcToFrame(vec);

    auto response = NotifyAndWaitForResponse(vec);
    if(response != ResponseStatus::Ok)
    {
        m_RecvData.clear();
        return std::unexpected(response == ResponseStatus::ModbusException ? ModbusError::ExceptionResponse :
            response == ResponseStatus::CrcError ? ModbusError::Crc : ModbusError::UnexpectedResponse);
    }
    const uint8_t function_code = write_count == 1 ? FC_WriteSingleRegister : FC_WriteMultipleRegister;
    const uint16_t echoed_value = write_count == 1 ? buffer[0] : write_count;
    const auto parsed = modbus::ParseWriteResponse(ToTransport(IsTcp()), m_RecvData, slave_id,
        function_code, write_offset, echoed_value, false,
        IsTcp() ? std::optional<uint16_t>(static_cast<uint16_t>((vec[0] << 8) | vec[1])) : std::nullopt);
    m_RecvData.clear();
    if(!parsed.Ok())
        return std::unexpected(ToModbusError(parsed.error));
    return vec;
}

std::expected<std::vector<uint16_t>, ModbusError> ModbusMasterSerialPort::ReadInputRegister(uint8_t slave_id, uint16_t read_offset, uint16_t read_count)
{
    const std::scoped_lock request_lock(m_RequestMutex);
    const auto frame = modbus::BuildReadRequest(ToTransport(IsTcp()), sequence_id, slave_id,
        FC_ReadInputRegister, read_offset, read_count);
    if(!frame.Ok())
        return std::unexpected(ToModbusError(frame.error));
    if(IsTcp())
        ++sequence_id;
    const auto response = NotifyAndWaitForResponse(frame.frame);
    if(response != ResponseStatus::Ok)
    {
        m_RecvData.clear();
        return std::unexpected(response == ResponseStatus::ModbusException ? ModbusError::ExceptionResponse :
            response == ResponseStatus::CrcError ? ModbusError::Crc : ModbusError::UnexpectedResponse);
    }
    const auto parsed = modbus::ParseReadRegistersResponse(ToTransport(IsTcp()), m_RecvData, slave_id,
        FC_ReadInputRegister, read_count, false, IsTcp() ? std::optional<uint16_t>(
            static_cast<uint16_t>((frame.frame[0] << 8) | frame.frame[1])) : std::nullopt);
    m_RecvData.clear();
    if(!parsed.Ok())
        return std::unexpected(ToModbusError(parsed.error));
    return parsed.registers;
}

std::expected<std::vector<uint16_t>, ModbusError> ModbusMasterSerialPort::ReadInputRegisters(uint8_t slave_id, uint16_t read_offset, uint16_t read_count)
{
    if(read_count == 0 || static_cast<uint32_t>(read_offset) + read_count > 0x10000)
        return std::unexpected(ModbusError::InvalidRequest);
    std::vector<uint16_t> result;
    size_t remaining = read_count;
    uint16_t offset = read_offset;
    while(remaining > 0)
    {
        size_t step = std::min<size_t>(remaining, MAX_HOLDING_REG_ONCE);
        auto tmp = ReadInputRegister(slave_id, offset, step);
        if(!tmp.has_value())
            return std::unexpected(tmp.error());
        if(tmp->size() != step)
            return std::unexpected(ModbusError::InvalidLength);

        result.insert(result.end(), tmp->begin(), tmp->end());
        offset    += step;
        remaining -= step;
    }
    return result;
}

ModbusCustomCommandResult ModbusMasterSerialPort::SendCustomCommand(const std::vector<uint8_t>& frame)
{
    const std::scoped_lock request_lock(m_RequestMutex);
    ModbusCustomCommandResult result;
    const auto response = NotifyAndWaitForResponse(frame);
    result.response = m_RecvData;
    m_RecvData.clear();
    if(response == ResponseStatus::ModbusException)
        result.error = ModbusError::ExceptionResponse;
    else if(response == ResponseStatus::CrcError)
        result.error = IsTcp() ? ModbusError::InvalidLength : ModbusError::Crc;
    else if(response != ResponseStatus::Ok)
        result.error = ModbusError::UnexpectedResponse;
    return result;
}

void ModbusMasterSerialPort::Init()
{
    if(is_enabled)
    {
        auto recv_f = [this](const char* data, unsigned int len) { OnUartDataReceived(data, len); };
        auto send_f = [this](CallbackAsyncSerial& serial_port) { OnDataSent(serial_port); };
        InitInternal("ModbusMasterSerialPort", SERIAL_PORT_TIMEOUT, SERIAL_PORT_EXCEPTION_TIMEOUT, recv_f, send_f, 0, false);
    }
    else
    {
        m_worker.reset();
    }
}

void ModbusMasterSerialPort::OnUartDataReceived(const char* data, unsigned int len)
{
    std::scoped_lock lock(m_RecvMutex);
    if(!data || len == 0)
        return;

    m_RawRecvData.insert(m_RawRecvData.end(),
        reinterpret_cast<const uint8_t*>(data), reinterpret_cast<const uint8_t*>(data) + len);

    size_t expected_size = 0;
    if(IsTcp())
    {
        if(m_RawRecvData.size() >= 6)
        {
            const uint16_t mbap_length = static_cast<uint16_t>((m_RawRecvData[4] << 8) | m_RawRecvData[5]);
            expected_size = 6 + mbap_length;
            if(expected_size > 260)
                expected_size = m_RawRecvData.size();
        }
    }
    else if(m_RawRecvData.size() >= 2)
    {
        const uint8_t function_code = m_RawRecvData[1];
        if((function_code & 0x80) != 0)
            expected_size = 5;
        else if(function_code >= FC_ReadCoilStatus && function_code <= FC_ReadInputRegister)
        {
            if(m_RawRecvData.size() >= 3)
                expected_size = 5 + m_RawRecvData[2];
        }
        else if(function_code == FC_ForceSingleCoil || function_code == FC_WriteSingleRegister ||
            function_code == FC_WriteMultipleRegister)
        {
            expected_size = 8;
        }
        else if(m_RawRecvData.size() >= 5)
        {
            // Custom function codes have no general length field. Deliver the
            // first complete CRC-bearing callback and let the caller inspect it.
            expected_size = m_RawRecvData.size();
        }
    }

    if(expected_size == 0 || m_RawRecvData.size() < expected_size)
        return;

    m_RawRecvData.resize(expected_size);
    if(IsTcp())
    {
        m_LastDataCrcOk = true;
        m_RecvData = m_RawRecvData;
    }
    else
    {
        const uint16_t actual_crc = static_cast<uint16_t>(m_RawRecvData[expected_size - 2] |
            (static_cast<uint16_t>(m_RawRecvData[expected_size - 1]) << 8));
        m_LastDataCrcOk = modbus::Crc16(m_RawRecvData.begin(), m_RawRecvData.end() - 2) == actual_crc;
        m_RecvData.assign(m_RawRecvData.begin(), m_RawRecvData.end() - 2);
    }
    m_RecvCv.notify_all();
}

void ModbusMasterSerialPort::OnDataSent(CallbackAsyncSerial& serial_port)
{
    if(!m_SentData.empty())
    {
        if (m_recorder && m_recorder->IsRecording())
        {
            m_recorder->CheckMaxEntries();
            m_recorder->RecordFrame(MODBUS_LOG_DIR_TX, ExtractFunctionCode(m_SentData),
                ModbusErrorType::MB_ERR_OK, m_SentData.data(), TrimmedLen(m_SentData.size()));
        }

        serial_port.write((const char*)m_SentData.data(), m_SentData.size());
        m_LastSentData = std::move(m_SentData);
        m_SentData.clear();
    }
}
