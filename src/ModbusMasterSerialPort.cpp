#include "pch_core.hpp"
#include "ModbusMasterSerialPort.hpp"
#include "Logger.hpp"
#include "Utils.hpp"
#include "ModbusProtocol.hpp"

using namespace std::chrono_literals;

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


ModbusMasterSerialPort::TransactionId ModbusMasterSerialPort::TransactionOf(
    const std::vector<uint8_t>& frame) const
{
    /* The two leading MBAP bytes. Guarded: the six copies of this indexed
       frame[0] and frame[1] without looking, which held for every frame they
       were given but said nothing about it. */
    if(!IsTcp() || frame.size() < 2)
        return std::nullopt;
    return static_cast<uint16_t>((frame[0] << 8) | frame[1]);
}

std::expected<ModbusMasterSerialPort::TransactionId, ModbusError>
ModbusMasterSerialPort::SendAndAwait(const std::vector<uint8_t>& frame)
{
    const auto response = NotifyAndWaitForResponse(frame);
    if(response != ResponseStatus::Ok)
    {
        m_RecvData.clear();
        return std::unexpected(response == ResponseStatus::ModbusException ? ModbusError::ExceptionResponse :
            response == ResponseStatus::CrcError ? ModbusError::Crc : ModbusError::UnexpectedResponse);
    }
    return TransactionOf(frame);
}

std::expected<ModbusMasterSerialPort::TransactionId, ModbusError>
ModbusMasterSerialPort::SendReadRequest(uint8_t slave_id, uint8_t function_code,
    uint16_t read_offset, uint16_t read_count)
{
    const auto frame = modbus::BuildReadRequest(ToTransport(IsTcp()), ConsumeSequenceId(),
        slave_id, function_code, read_offset, read_count);
    if(!frame.Ok())
        return std::unexpected(ToModbusError(frame.error));

    return SendAndAwait(frame.frame);
}

std::expected<std::vector<uint8_t>, ModbusError> ModbusMasterSerialPort::ReadCoilStatus(uint8_t slave_id, uint16_t read_offset, uint16_t read_count)
{
    const std::scoped_lock request_lock(m_RequestMutex);
    const auto transaction = SendReadRequest(slave_id, FC_ReadCoilStatus, read_offset, read_count);
    if(!transaction)
        return std::unexpected(transaction.error());

    const auto parsed = modbus::ParseReadBitsResponse(ToTransport(IsTcp()), m_RecvData, slave_id,
        FC_ReadCoilStatus, read_count, false, *transaction);
    m_RecvData.clear();
    if(!parsed.Ok())
        return std::unexpected(ToModbusError(parsed.error));
    return parsed.packed_bits;
}

std::expected<std::vector<uint8_t>, ModbusError> ModbusMasterSerialPort::ForceSingleCoil(uint8_t slave_id, uint16_t write_offset, bool status)
{
    const std::scoped_lock request_lock(m_RequestMutex);
    auto frame = modbus::BuildWriteCoilRequest(ToTransport(IsTcp()), ConsumeSequenceId(),
        slave_id, write_offset, status);
    if(!frame.Ok())
        return std::unexpected(ToModbusError(frame.error));
    std::vector<uint8_t>& vec = frame.frame;

    const auto transaction = SendAndAwait(vec);
    if(!transaction)
        return std::unexpected(transaction.error());

    const auto parsed = modbus::ParseWriteResponse(ToTransport(IsTcp()), m_RecvData, slave_id,
        FC_ForceSingleCoil, write_offset, status ? uint16_t{0xFF00} : uint16_t{0}, false,
        *transaction);
    m_RecvData.clear();
    if(!parsed.Ok())
        return std::unexpected(ToModbusError(parsed.error));
    return vec;
}

std::expected<std::vector<uint8_t>, ModbusError> ModbusMasterSerialPort::ReadInputStatus(uint8_t slave_id, uint16_t read_offset, uint16_t read_count)
{
    const std::scoped_lock request_lock(m_RequestMutex);
    const auto transaction = SendReadRequest(slave_id, FC_ReadInputStatus, read_offset, read_count);
    if(!transaction)
        return std::unexpected(transaction.error());

    const auto parsed = modbus::ParseReadBitsResponse(ToTransport(IsTcp()), m_RecvData, slave_id,
        FC_ReadInputStatus, read_count, false, *transaction);
    m_RecvData.clear();
    if(!parsed.Ok())
        return std::unexpected(ToModbusError(parsed.error));
    return parsed.packed_bits;
}

std::expected<std::vector<uint16_t>, ModbusError> ModbusMasterSerialPort::ReadHoldingRegister(uint8_t slave_id, uint16_t read_offset, uint16_t read_count)
{
    const std::scoped_lock request_lock(m_RequestMutex);
    const auto transaction = SendReadRequest(slave_id, FC_ReadHoldingRegister, read_offset, read_count);
    if(!transaction)
        return std::unexpected(transaction.error());

    const auto parsed = modbus::ParseReadRegistersResponse(ToTransport(IsTcp()), m_RecvData, slave_id,
        FC_ReadHoldingRegister, read_count, false, *transaction);
    m_RecvData.clear();
    if(!parsed.Ok())
        return std::unexpected(ToModbusError(parsed.error));
    return parsed.registers;
}

std::expected<std::vector<uint16_t>, ModbusError> ModbusMasterSerialPort::ReadHoldingRegisters(uint8_t slave_id, uint16_t read_offset, uint16_t read_count)
{
    return ChunkedRead(read_offset, read_count,
        [this, slave_id](uint16_t offset, uint16_t count)
        {
            return ReadHoldingRegister(slave_id, offset, count);
        });
}

std::expected<std::vector<uint8_t>, ModbusError> ModbusMasterSerialPort::WriteHoldingRegister(uint8_t slave_id, uint16_t write_offset, uint16_t write_count, std::vector<uint16_t> buffer)
{
    const std::scoped_lock request_lock(m_RequestMutex);
    /* The count/size mismatch check stays here: it is about this API's two
       parameters describing one buffer, not about the wire. */
    if(write_count != buffer.size())
        return std::unexpected(ModbusError::InvalidRequest);
    auto frame = modbus::BuildWriteRegistersRequest(ToTransport(IsTcp()), ConsumeSequenceId(),
        slave_id, write_offset, buffer);
    if(!frame.Ok())
        return std::unexpected(ToModbusError(frame.error));
    std::vector<uint8_t>& vec = frame.frame;

    const auto transaction = SendAndAwait(vec);
    if(!transaction)
        return std::unexpected(transaction.error());

    const uint8_t function_code = write_count == 1 ? FC_WriteSingleRegister : FC_WriteMultipleRegister;
    const uint16_t echoed_value = write_count == 1 ? buffer[0] : write_count;
    const auto parsed = modbus::ParseWriteResponse(ToTransport(IsTcp()), m_RecvData, slave_id,
        function_code, write_offset, echoed_value, false, *transaction);
    m_RecvData.clear();
    if(!parsed.Ok())
        return std::unexpected(ToModbusError(parsed.error));
    return vec;
}

std::expected<std::vector<uint16_t>, ModbusError> ModbusMasterSerialPort::ReadInputRegister(uint8_t slave_id, uint16_t read_offset, uint16_t read_count)
{
    const std::scoped_lock request_lock(m_RequestMutex);
    const auto transaction = SendReadRequest(slave_id, FC_ReadInputRegister, read_offset, read_count);
    if(!transaction)
        return std::unexpected(transaction.error());

    const auto parsed = modbus::ParseReadRegistersResponse(ToTransport(IsTcp()), m_RecvData, slave_id,
        FC_ReadInputRegister, read_count, false, *transaction);
    m_RecvData.clear();
    if(!parsed.Ok())
        return std::unexpected(ToModbusError(parsed.error));
    return parsed.registers;
}

std::expected<std::vector<uint16_t>, ModbusError> ModbusMasterSerialPort::ReadInputRegisters(uint8_t slave_id, uint16_t read_offset, uint16_t read_count)
{
    return ChunkedRead(read_offset, read_count,
        [this, slave_id](uint16_t offset, uint16_t count)
        {
            return ReadInputRegister(slave_id, offset, count);
        });
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
        /* The transport hands out a size_t length; narrowing it is the caller's job. */
        auto recv_f = [this](const char* data, std::size_t len) { OnUartDataReceived(data, static_cast<unsigned int>(len)); };
        auto send_f = [this](CallbackAsyncSerial& serial_port) { OnDataSent(serial_port); };
        InitInternal("ModbusMasterSerialPort", SERIAL_PORT_TIMEOUT, SERIAL_PORT_EXCEPTION_TIMEOUT, recv_f, send_f, 0, false);
    }
    else
    {
        /* The other two ports tear down through the base class. Resetting the
           worker directly skipped DestroyWorkerThread, so the condition
           variable was never notified and the pending-notification flag stayed
           set from the previous session. */
        DeInitInternal();
    }
}

void ModbusMasterSerialPort::OnUartDataReceived(const char* data, unsigned int len)
{
    std::scoped_lock lock(m_RecvMutex);
    if(!data || len == 0)
        return;

    m_RawRecvData.insert(m_RawRecvData.end(),
        reinterpret_cast<const uint8_t*>(data), reinterpret_cast<const uint8_t*>(data) + len);

    /* Was a six-branch chain here, where the only way to exercise it was to
       drive a port. The decisions are modbus::ExpectedResponseLength now,
       tested next to the builders whose frames it measures. */
    const size_t expected_size = modbus::ExpectedResponseLength(ToTransport(IsTcp()), m_RawRecvData);

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
