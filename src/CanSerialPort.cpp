#include "pch.hpp"

#include "CanDeviceFactory.hpp"

constexpr size_t TX_QUEUE_MAX_SIZE = 100;
constexpr size_t RX_CIRCBUFF_SIZE = 1024;  /* Bytes */
constexpr size_t CAN_SERIAL_TX_BUFFER_SIZE = 64;
constexpr auto CAN_SERIAL_PORT_TIMEOUT = 5000ms;
constexpr auto CAN_SERIAL_PORT_EXCEPTION_TIMEOUT = 1000ms;
constexpr auto SEND_DELAY_BETWEEN_FRAMES = 100us;

CanSerialPort::CanSerialPort() : m_CircBuff(RX_CIRCBUFF_SIZE),
    m_DeviceFactory(std::make_unique<CanDeviceFactory>(m_CircBuff))
{

}

CanSerialPort::~CanSerialPort()
{

}

void CanSerialPort::Init()
{
    if(is_enabled)
    {
        auto recv_f = std::bind(&CanSerialPort::OnDataReceived, this, std::placeholders::_1, std::placeholders::_2);
        auto send_f = std::bind(&CanSerialPort::OnDataSent, this, std::placeholders::_1);
        InitInternal("CanSerialPort", CAN_SERIAL_PORT_TIMEOUT, CAN_SERIAL_PORT_EXCEPTION_TIMEOUT, recv_f, send_f);
        m_Device = m_DeviceFactory->Create(m_DeviceType);
    }
    else
    {
        DeInitInternal();
    }
}

void CanSerialPort::SetDevice(std::unique_ptr<ICanDevice>&& device)
{
    m_Device = std::move(device);
}

void CanSerialPort::SetDeviceFactory(std::unique_ptr<ICanDeviceFactory> factory)
{
    if(factory == nullptr)
        throw std::invalid_argument("CAN device factory must not be null");
    m_DeviceFactory = std::move(factory);
}

void CanSerialPort::SetListener(ICanTransportListener* listener) noexcept
{
    m_Listener.store(listener, std::memory_order_release);
}

void CanSerialPort::Send(uint32_t frame_id, std::span<const uint8_t> data)
{
    const auto length = static_cast<uint8_t>(std::min(data.size(), MAX_CAN_FRAME_DATA_LEN));
    AddToTxQueue(frame_id, length, data.data());
}

void CanSerialPort::AddToTxQueue(uint32_t frame_id, uint8_t data_len, const uint8_t* data)
{
    if(!data || !data_len)
        return;
    std::unique_lock lock(m_mutex);
    m_TxQueue.push(std::make_unique<CanData>(frame_id, data_len, data));

    if(m_TxQueue.size() > TX_QUEUE_MAX_SIZE)
    {
        //LOG(LogLevel::Error, "Queue overflow");
        m_TxQueue.pop();
    }
    NotifiyMainThread();
}

void CanSerialPort::OnDataReceived(const char* data, unsigned int len)
{
    std::scoped_lock guard(m_RxMutex);
    m_CircBuff.insert(m_CircBuff.end(), data, data + len);
    NotifiyMainThread();
}

void CanSerialPort::OnDataSent(CallbackAsyncSerial& serial_port)
{
    m_Device->ProcessReceivedFrames(m_RxMutex, [this](uint32_t frame_id, uint8_t data_len, uint8_t* data)
    {
        if(auto* listener = m_Listener.load(std::memory_order_acquire))
            listener->OnFrameReceived(frame_id, data_len, data);
    });
    SendPendingCanFrames(serial_port);
}

void CanSerialPort::SendPendingCanFrames(CallbackAsyncSerial& serial_port)
{
    //DBG("txssize: %lld\n", m_TxQueue.size());
    while(!m_TxQueue.empty())
    {
        std::shared_ptr<CanData> data_ptr = m_TxQueue.front();
        bool is_remove = false;

        {
            std::scoped_lock lock(m_mutex);
            char data[CAN_SERIAL_TX_BUFFER_SIZE];
            size_t size = m_Device->PrepareSendDataFormat(data_ptr, data, sizeof(data), is_remove);

            if(size)
                serial_port.write((const char*)&data, size);
        }

        if(auto* listener = m_Listener.load(std::memory_order_acquire))
            listener->OnFrameSent(data_ptr->frame_id, data_ptr->data_len, data_ptr->data);

        if(is_remove)
            m_TxQueue.pop();

        std::this_thread::sleep_for(SEND_DELAY_BETWEEN_FRAMES);  /* This delay is needed because UART timeout won't happen if everything is sent at once */
    }
}
