#include "pch_core.hpp"
#include "CanDeviceStm32.hpp"
#include "Logger.hpp"
#include "Utils.hpp"

CanDeviceStm32::CanDeviceStm32() = default;

CanDeviceStm32::~CanDeviceStm32() = default;

void CanDeviceStm32::DecodeReceivedBytes(std::span<const std::uint8_t> received, const CanFrameReceiver& receiver)
{
    for(auto& frame : m_Decoder.Feed(std::vector<uint8_t>(received.begin(), received.end())))
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
