#pragma once

#include <inttypes.h>
#include <ICanDevice.hpp>
#include "CanCodecs.hpp"

constexpr uint32_t MAGIC_NUMBER_SEND_DATA_TO_CAN_BUS = can_codec::Stm32SendMagic;
constexpr uint32_t MAGIC_NUMBER_RECV_DATA_FROM_CAN_BUS = can_codec::Stm32ReceiveMagic;

class CanDeviceStm32 : public ICanDevice
{
public:
    CanDeviceStm32(boost::circular_buffer<char>& CircBuff);
    ~CanDeviceStm32();

    void ProcessReceivedFrames(std::mutex& rx_mutex, const CanFrameReceiver& receiver) override;
    size_t PrepareSendDataFormat(const std::shared_ptr<CanData>& data_ptr, char* out, size_t size, bool& remove_from_queue) override;

private:
    boost::circular_buffer<char>& m_CircBuff;
    can_codec::Stm32StreamDecoder m_Decoder;
};
