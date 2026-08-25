#pragma once

#include <inttypes.h>
#include <ICanDevice.hpp>
#include "CanCodecs.hpp"

class CanDeviceLawicel : public ICanDevice
{
public:
    CanDeviceLawicel();
    ~CanDeviceLawicel();

    void DecodeReceivedBytes(std::span<const std::uint8_t> received, const CanFrameReceiver& receiver) override;
    size_t PrepareSendDataFormat(const std::shared_ptr<CanData>& data_ptr, char* out, size_t size, bool& remove_from_queue) override;

private:
    can_codec::LawicelStreamDecoder m_Decoder;
    // !\brief How far through kHandshake the adapter has been walked. At
    // the table's size, the handshake is done and frames flow.
    size_t m_HandshakeStep = 0;
};
