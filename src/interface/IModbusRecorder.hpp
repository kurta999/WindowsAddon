#pragma once
#include <cstdint>

/* Error codes returned by / logged from the Modbus stack */
enum ModbusErrorType : uint8_t
{
    MB_ERR_OK = 0,
    MB_ERR_CRC,
    MB_ERR_TIMEOUT,
    MB_ERR_ILLEGAL_FUNCTION    = 129,
    MB_ERR_ILLEGAL_DATA_ADDRESS = 130,
    MB_ERR_ILLEGAL_DATA_VALUE   = 131,
    MB_ERR_SLAVE_DEVICE_FAILURE = 132,
    MB_ERR_ACK                  = 133,
    MB_ERR_SLAVE_DEVICE_BUSY    = 134,
    MB_ERR_NAK,
    MB_ERR_MEMORY_PARITY_ERROR,
    MB_ERR_GATEWAY_UNAVAILABLE  = 143,
    MB_ERR_GATEWAY_TARGET_FAILED = 144,
};

constexpr uint8_t MODBUS_LOG_DIR_RX = 0;
constexpr uint8_t MODBUS_LOG_DIR_TX = 1;

/* Injected into ModbusMasterSerialPort to break the direct dependency on ModbusEntryHandler */
class IModbusRecorder
{
public:
    virtual ~IModbusRecorder() = default;

    virtual bool IsRecording() const = 0;
    virtual bool IsReady() const = 0;
    virtual void RecordFrame(uint8_t direction, uint8_t fc, ModbusErrorType error, const uint8_t* data, size_t len) = 0;
    virtual void CheckMaxEntries() = 0;
};
