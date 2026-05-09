#include "pch.hpp"

#pragma pack(push, 1)
typedef struct
{
    uint32_t magic_number; // 0xAABBCCDD
    uint32_t frame_id;
    uint8_t data_len;
    uint8_t data[8];
    uint16_t crc;
} UartCanData;
#pragma pack(pop)

CanDeviceStm32::CanDeviceStm32(boost::circular_buffer<char>& CircBuff) : 
    m_CircBuff(CircBuff)
{

}

CanDeviceStm32::~CanDeviceStm32()
{
    
}

void CanDeviceStm32::ProcessReceivedFrames(std::mutex& rx_mutex)
{
    std::unique_lock lock(rx_mutex);
    while(m_CircBuff.size() >= sizeof(UartCanData))
    {
        do
        {
            char uart_data[sizeof(UartCanData)] = {};
            std::copy(m_CircBuff.begin(), m_CircBuff.begin() + sizeof(UartCanData), uart_data);

            UartCanData* d = reinterpret_cast<UartCanData*>(uart_data);
            if(d->magic_number == MAGIC_NUMBER_RECV_DATA_FROM_CAN_BUS)
            {
                uint16_t crc = utils::crc16_modbus(reinterpret_cast<void*>(uart_data), sizeof(UartCanData) - sizeof(UartCanData::crc));
                if(crc == d->crc)
                {
                    if(d->data_len <= sizeof(d->data))
                        CanSerialPort::Get()->AddToRxQueue(d->frame_id, d->data_len, d->data);
                    else
                        LOG(LogLevel::Warning, "Invalid CAN data_len: {}", d->data_len);
                }
                else
                {
                    std::string hex;
                    LOG(LogLevel::Verbose, "CRC mismatch, recv - calculated: {:X} != {:X}", d->crc, crc);
                    utils::ConvertHexBufferToString(reinterpret_cast<const char*>(d->data), sizeof(d->data), hex);
                    LOG(LogLevel::Verbose, "MagicNumber: {:X}, FrameID: {:X}, DataLen: {}", d->magic_number, d->frame_id, d->data_len);
                    hex.clear();
                    utils::ConvertHexBufferToString(uart_data, sizeof(uart_data), hex);
                    LOG(LogLevel::Verbose, "Full Data buffer: {}", hex);
                }
                m_CircBuff.erase(m_CircBuff.begin(), m_CircBuff.begin() + sizeof(UartCanData));
            }
            else
            {
                LOG(LogLevel::Verbose, "Invalid magic number: {:X}", d->magic_number);
                m_CircBuff.erase(m_CircBuff.begin());
            }
        } while(m_CircBuff.size() >= sizeof(UartCanData));  /* This second nested loop is needed for recovering when invalid data is received */
    }
}

size_t CanDeviceStm32::PrepareSendDataFormat(const std::shared_ptr<CanData>& data_ptr, char* out, size_t max_size, bool& remove_from_queue)
{
    UartCanData* d = reinterpret_cast<UartCanData*>(out);
    assert(max_size >= sizeof(*d));

    d->magic_number = MAGIC_NUMBER_SEND_DATA_TO_CAN_BUS;
    d->frame_id = data_ptr->frame_id;
    d->data_len = data_ptr->data_len;
    memcpy(d->data, data_ptr->data, d->data_len);
    d->crc = utils::crc16_modbus((void*)d, sizeof(*d) - sizeof(UartCanData::crc));
    remove_from_queue = true;
    return sizeof(*d);
}