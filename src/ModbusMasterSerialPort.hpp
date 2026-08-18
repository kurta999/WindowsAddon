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

    uint16_t m_ResponseTimeout = 5000;

    size_t GetTimeoutPackets() { return timeout_packets; }
    void ResetTimeoutPackets() { timeout_packets = 0; }

private:
    enum ModbusFunctionCodes : uint8_t
    {
        FC_ReadCoilStatus      = 1,
        FC_ReadInputStatus     = 2,
        FC_ReadHoldingRegister = 3,
        FC_ReadInputRegister   = 4,
        FC_ForceSingleCoil     = 5,
        FC_WriteSingleRegister = 6,
        FC_WriteMultipleRegister = 16,
    };

    enum ResponseStatus : uint8_t
    {
        Ok,
        Timeout,
        ModbusException,
        CrcError
    };

    void SetupHeader(std::vector<uint8_t>& vec, uint8_t slave_id, uint16_t fcode, uint16_t len);
    void AddCrcToFrame(std::vector<uint8_t>& vec);
    ResponseStatus NotifyAndWaitForResponse(const std::vector<uint8_t>& vec);
    void DoCleanup(std::vector<uint8_t>& recv_data);
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
