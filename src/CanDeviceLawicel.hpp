#pragma once

#include <inttypes.h>
#include <ICanDevice.hpp>
#include "CanCodecs.hpp"

class CanDeviceLawicel : public ICanDevice
{
public:
    CanDeviceLawicel(boost::circular_buffer<char>& CircBuff);
    ~CanDeviceLawicel();

    void ProcessReceivedFrames(std::mutex& rx_mutex, const CanFrameReceiver& receiver) override;
    size_t PrepareSendDataFormat(const std::shared_ptr<CanData>& data_ptr, char* out, size_t size, bool& remove_from_queue) override;

private:
    boost::circular_buffer<char>& m_CircBuff;
    can_codec::LawicelStreamDecoder m_Decoder;
    uint8_t device_state = 0;
};
