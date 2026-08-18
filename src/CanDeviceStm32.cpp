#include "pch.hpp"

CanDeviceStm32::CanDeviceStm32(boost::circular_buffer<char>& CircBuff) : 
    m_CircBuff(CircBuff)
{

}

CanDeviceStm32::~CanDeviceStm32()
{
    
}

void CanDeviceStm32::ProcessReceivedFrames(std::mutex& rx_mutex, const CanFrameReceiver& receiver)
{
    std::unique_lock lock(rx_mutex);
    std::vector<uint8_t> received;
    received.reserve(m_CircBuff.size());
    for(const char byte : m_CircBuff)
        received.push_back(static_cast<uint8_t>(byte));
    m_CircBuff.clear();
    lock.unlock();

    for(auto& frame : m_Decoder.Feed(received))
    {
        receiver(frame.id, static_cast<uint8_t>(frame.data.size()), frame.data.data());
    }
}

size_t CanDeviceStm32::PrepareSendDataFormat(const std::shared_ptr<CanData>& data_ptr, char* out, size_t max_size, bool& remove_from_queue)
{
    const can_codec::Frame frame{data_ptr->frame_id,
        std::vector<uint8_t>(data_ptr->data, data_ptr->data + data_ptr->data_len)};
    const auto encoded = can_codec::EncodeStm32(frame);
    if(!encoded || max_size < encoded->size())
        return 0;
    std::copy(encoded->begin(), encoded->end(), reinterpret_cast<uint8_t*>(out));
    remove_from_queue = true;
    return encoded->size();
}
