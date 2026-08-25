#pragma once

#include "utils/CSingleton.hpp"
#include <atomic>
#include <condition_variable>
#include <string>
#include <expected>
#include <semaphore>
#include <boost/circular_buffer.hpp>
#include <vector>
#include <expected>
#include <map>
#include <mutex>
#include <optional>
#include "utils/AsyncSerial.hpp"
#include "SerialPortBase.hpp"
#include "IModbusEntry.hpp"
#include "IModbusRecorder.hpp"
#include "ModbusProtocol.hpp"

enum class ModbusError
{
    WrongId,
    WrongTransactionId,
    InvalidRequest,
    Crc,
    InvalidLength,
    ExceptionResponse,
    UnexpectedResponse,
};

struct ModbusCustomCommandResult
{
    std::vector<uint8_t> response;
    std::optional<ModbusError> error;
    bool Ok() const { return !error.has_value(); }
};

class ModbusMasterSerialPort : public SerialPortBase
{
public:
    ModbusMasterSerialPort();
    ~ModbusMasterSerialPort();

    void SetStopToken(std::stop_token& token) { m_stopToken = &token; }
    void SetRecorder(IModbusRecorder* recorder) { m_recorder = recorder; }

    void Init();
    void OnUartDataReceived(const char* data, unsigned int len);
    void OnDataSent(CallbackAsyncSerial& serial_port);

    std::expected<std::vector<uint8_t>, ModbusError> ReadCoilStatus(uint8_t slave_id, uint16_t read_offset, uint16_t read_count);
    std::expected<std::vector<uint8_t>, ModbusError> ForceSingleCoil(uint8_t slave_id, uint16_t write_offset, bool status);
    std::expected<std::vector<uint8_t>, ModbusError> ReadInputStatus(uint8_t slave_id, uint16_t read_offset, uint16_t read_count);
    std::expected<std::vector<uint16_t>, ModbusError> ReadHoldingRegister(uint8_t slave_id, uint16_t read_offset, uint16_t read_count);
    std::expected<std::vector<uint16_t>, ModbusError> ReadHoldingRegisters(uint8_t slave_id, uint16_t read_offset, uint16_t read_count);
    std::expected<std::vector<uint8_t>, ModbusError> WriteHoldingRegister(uint8_t slave_id, uint16_t write_offset, uint16_t write_count, std::vector<uint16_t> buffer);
    std::expected<std::vector<uint16_t>, ModbusError> ReadInputRegister(uint8_t slave_id, uint16_t read_offset, uint16_t read_count);
    std::expected<std::vector<uint16_t>, ModbusError> ReadInputRegisters(uint8_t slave_id, uint16_t read_offset, uint16_t read_count);
    ModbusCustomCommandResult SendCustomCommand(const std::vector<uint8_t>& frame);

    /* Read by the polling worker while the settings load and the GUI spin
       control write it. */
    std::atomic<uint16_t> m_ResponseTimeout = 5000;

    size_t GetTimeoutPackets() { return timeout_packets; }
    void ResetTimeoutPackets() { timeout_packets = 0; }

private:
    /* The function codes moved to modbus:: in ModbusProtocol.hpp, where the
       builders and the response-length logic that dispatch on them live. The
       using-declaration keeps every FC_ name in this file meaning what it
       meant. */
    using enum modbus::ModbusFunctionCodes;

    enum ResponseStatus : uint8_t
    {
        Ok,
        Timeout,
        ModbusException,
        CrcError
    };

    // !\brief The transaction id a TCP response has to echo back, taken from
    // the request's MBAP header. An RTU frame carries none.
    using TransactionId = std::optional<uint16_t>;

    [[nodiscard]] TransactionId TransactionOf(const std::vector<uint8_t>& frame) const;

    // !\brief The transaction id for the next request. TCP burns one per
    // exchange; RTU has none to burn. Was `if(IsTcp()) ++sequence_id;`
    // written out after every build call.
    [[nodiscard]] uint16_t ConsumeSequenceId()
    {
        return IsTcp() ? sequence_id++ : sequence_id;
    }

    // !\brief Send a framed request and wait for its answer.
    //
    // The six request functions each wrote out the same failure mapping, and
    // each had to remember to clear m_RecvData before returning from it.
    // !\return What the response must echo, or the error the exchange failed with.
    [[nodiscard]] std::expected<TransactionId, ModbusError> SendAndAwait(
        const std::vector<uint8_t>& frame);

    // !\brief Build one of the four read requests, send it, and wait.
    [[nodiscard]] std::expected<TransactionId, ModbusError> SendReadRequest(
        uint8_t slave_id, uint8_t function_code, uint16_t read_offset, uint16_t read_count);

    // !\brief Read `read_count` registers in chunks the protocol allows, in order.
    //
    // ReadHoldingRegisters and ReadInputRegisters were this loop twice,
    // character for character apart from which single read they call.
    template <typename ReadOne>
    [[nodiscard]] std::expected<std::vector<uint16_t>, ModbusError> ChunkedRead(
        uint16_t read_offset, uint16_t read_count, ReadOne read_one)
    {
        if(read_count == 0 || static_cast<uint32_t>(read_offset) + read_count > 0x10000)
            return std::unexpected(ModbusError::InvalidRequest);

        std::vector<uint16_t> result;
        size_t remaining = read_count;
        uint16_t offset = read_offset;
        while(remaining > 0)
        {
            const uint16_t step = static_cast<uint16_t>(
                std::min<size_t>(remaining, kMaxRegistersPerRequest));
            const auto chunk = read_one(offset, step);
            if(!chunk.has_value())
                return std::unexpected(chunk.error());
            if(chunk->size() != step)
                return std::unexpected(ModbusError::InvalidLength);

            result.insert(result.end(), chunk->begin(), chunk->end());
            offset    += step;
            remaining -= step;
        }
        return result;
    }

    // !\brief The most registers one request may carry. Both chunked reads used
    // this same limit, including the input-register one whose name said
    // holding.
    static constexpr size_t kMaxRegistersPerRequest = 120;

    ResponseStatus NotifyAndWaitForResponse(const std::vector<uint8_t>& vec);
    bool WaitForResponse();
    uint8_t ExtractFunctionCode(const std::vector<uint8_t>& data) const;
    size_t TrimmedLen(size_t raw_len) const;

    std::map<uint8_t, size_t> modbusErrorCount;

    std::stop_token* m_stopToken = nullptr;
    std::mutex m_RequestMutex;
    std::mutex m_RecvMutex;
    std::condition_variable_any m_RecvCv;
    std::vector<uint8_t> m_RawRecvData;
    std::vector<uint8_t> m_RecvData;
    std::vector<uint8_t> m_SentData;
    std::vector<uint8_t> m_LastSentData;
    bool m_LastDataCrcOk = true;
    uint16_t sequence_id = 0;
    size_t timeout_packets = 0;
    IModbusRecorder* m_recorder = nullptr;
};
