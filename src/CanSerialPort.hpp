#pragma once

#include "utils/AsyncSerial.hpp"

#include "utils/CSingleton.hpp"
#include <atomic>
#include <condition_variable>
#include <string>
#include <semaphore>
#include <boost/circular_buffer.hpp>
#include <ICanDevice.hpp>
#include <ICanDeviceFactory.hpp>
#include <ICanTransport.hpp>
#include "SerialPortBase.hpp"

class CallbackAsyncSerial;
// !\brief The CAN transport. Everything that speaks CAN already receives it
// as ICanTransport&; the singleton was down to being a locator for two panels.
class CanSerialPort : public SerialPortBase, public ICanTransport
{
public:
    CanSerialPort();
    ~CanSerialPort();

    // !\brief Initialize CanSerialPort
    void Init();

    // !\brief Set CAN Device Type
    void SetDeviceType(CanDeviceType device_type) { m_DeviceType = device_type; }

    // !\brief Get CAN Device Type
    CanDeviceType GetDeviceType() const { return m_DeviceType; }

    // !\brief Set internal CAN device
    void SetDevice(std::unique_ptr<ICanDevice>&& device);

    // !\brief Replace protocol strategy creation (primarily for composition/tests)
    void SetDeviceFactory(std::unique_ptr<ICanDeviceFactory> factory);

    // !\brief Add CAN frame to TX queue
    void AddToTxQueue(uint32_t frame_id, uint8_t data_len, const uint8_t* data);

    // ICanTransport
    void SetListener(ICanTransportListener* listener) noexcept override;
    void Send(uint32_t frame_id, std::span<const uint8_t> data) override;

    // !\brief Send pending CAN Frames from the internal buffer
    void SendPendingCanFrames(CallbackAsyncSerial& serial_port);

private:
    // !\brief Called when data was received via serial port (called by boost::asio::read_some)
    // !\param serial_port [in] Pointer to received data
    // !\param len [in] Received data length
    void OnDataReceived(const char* data, unsigned int len);

    // !\brief On data sent
    void OnDataSent(CallbackAsyncSerial& serial_port);

    // !\brief Mutex for received data processing
    std::mutex m_RxMutex;

    // !\brief Circular buffer for received data
    boost::circular_buffer<char> m_CircBuff;

    // !\brief CAN Tx Queue
    std::queue<std::shared_ptr<CanData>> m_TxQueue;

    // !\brief CAN Device
    std::unique_ptr<ICanDevice> m_Device = nullptr;

    // !\brief Creates the selected CAN wire-protocol strategy
    std::unique_ptr<ICanDeviceFactory> m_DeviceFactory;

    // !\brief CAN Device type
    CanDeviceType m_DeviceType = CanDeviceType::STM32;

    std::atomic<ICanTransportListener*> m_Listener = nullptr;
};
