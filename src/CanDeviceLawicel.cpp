#include "pch.hpp"

constexpr size_t CAN_SERIAL_RESPONSE_BUFFER_SIZE = 64;

constexpr const char MESSAGE_TRANSMIT_STANDARD_FRAME = 't';
constexpr const char MESSAGE_TRANSMIT_EXTENDED_FRAME = 'T';
constexpr const char MESSAGE_TRANSMIT_VERSION_INFO = 'V';

CanDeviceLawicel::CanDeviceLawicel(boost::circular_buffer<char>& CircBuff) :
    m_CircBuff(CircBuff)
{

}

CanDeviceLawicel::~CanDeviceLawicel()
{

}

void CanDeviceLawicel::ProcessReceivedFrames(std::mutex& rx_mutex, const CanFrameReceiver& receiver)
{
    std::unique_lock lock(rx_mutex);
    std::string received(m_CircBuff.begin(), m_CircBuff.end());
    m_CircBuff.clear();
    lock.unlock();
    for(auto& frame : m_Decoder.Feed(received))
    {
        receiver(frame.id, static_cast<uint8_t>(frame.data.size()), frame.data.data());
    }
}

size_t CanDeviceLawicel::PrepareSendDataFormat(const std::shared_ptr<CanData>& data_ptr, char* out, size_t max_size, bool& remove_from_queue)
{
    size_t send_size = 0;
    switch(device_state)
    {
        case 0:  /* Initial CR */
        {
            send_size = 1;
            memcpy(out, "\r", send_size);
            device_state++;
            std::this_thread::sleep_for(150ms);
            break;
        }
        case 1:  /* Get version */
        {
            send_size = 2;
            memcpy(out, "V\r", send_size);  
            device_state++;
            std::this_thread::sleep_for(150ms);
            break;
        }
        case 2:  /* CAN Baudrate 500Kbps */
        {
            send_size = 3;
            memcpy(out, "S6\r", send_size);
            device_state++;
            std::this_thread::sleep_for(50ms);
            break;
        }
        case 3:  /* Open CAN channel */
        {
            send_size = 2;
            memcpy(out, "O\r", send_size);
            device_state++;
            std::this_thread::sleep_for(200ms);
            break;
        }
        case 4:  /* Send data to CAN bus */
        {
            remove_from_queue = true;
            const can_codec::Frame frame{data_ptr->frame_id,
                std::vector<uint8_t>(data_ptr->data, data_ptr->data + data_ptr->data_len)};
            const auto encoded = can_codec::EncodeLawicel(frame);
            if(!encoded || encoded->size() > max_size)
                return 0;
            send_size = encoded->size();
            memcpy(out, encoded->data(), encoded->size());
            break;
        }
        default:
        {
            assert(false);
            break;
        }
    }
    return send_size;
}
