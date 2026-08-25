#include "pch_core.hpp"
#include "CanDeviceLawicel.hpp"
#include "Logger.hpp"
#include "Utils.hpp"

using namespace std::chrono_literals;

constexpr size_t CAN_SERIAL_RESPONSE_BUFFER_SIZE = 64;

constexpr const char MESSAGE_TRANSMIT_STANDARD_FRAME = 't';
constexpr const char MESSAGE_TRANSMIT_EXTENDED_FRAME = 'T';
constexpr const char MESSAGE_TRANSMIT_VERSION_INFO = 'V';

namespace
{
/* The wake-up sequence a Lawicel adapter wants before it will carry frames,
   one row per exchange. This was a switch over a bare uint8_t that ++'d its
   way through five case labels, where adding a step meant renumbering the
   comments and nothing named what state 2 was. */
struct HandshakeCommand
{
    std::string_view command;
    /* How long the adapter needs to chew on it before the next byte. */
    std::chrono::milliseconds settle;
};

constexpr HandshakeCommand kHandshake[] = {
    { "\r",   std::chrono::milliseconds(150) },  /* flush the adapter's line buffer */
    { "V\r",  std::chrono::milliseconds(150) },  /* version query, as a liveness probe */
    { "S6\r", std::chrono::milliseconds(50) },   /* 500 kbit/s */
    { "O\r",  std::chrono::milliseconds(200) },  /* open the CAN channel */
};
}

CanDeviceLawicel::CanDeviceLawicel() = default;

CanDeviceLawicel::~CanDeviceLawicel() = default;

void CanDeviceLawicel::DecodeReceivedBytes(std::span<const std::uint8_t> received, const CanFrameReceiver& receiver)
{
    const std::string bytes(received.begin(), received.end());
    for(auto& frame : m_Decoder.Feed(bytes))
    {
        receiver(frame.id, static_cast<uint8_t>(frame.data.size()), frame.data.data());
    }
}

size_t CanDeviceLawicel::PrepareSendDataFormat(const std::shared_ptr<CanData>& data_ptr, char* out, size_t max_size, bool& remove_from_queue)
{
    if(m_HandshakeStep < std::size(kHandshake))
    {
        const HandshakeCommand& step = kHandshake[m_HandshakeStep];
        if(step.command.size() > max_size)
            return 0;
        memcpy(out, step.command.data(), step.command.size());
        ++m_HandshakeStep;
        std::this_thread::sleep_for(step.settle);
        return step.command.size();
    }

    remove_from_queue = true;
    const can_codec::Frame frame{data_ptr->frame_id,
        std::vector<uint8_t>(data_ptr->data, data_ptr->data + data_ptr->data_len)};
    const auto encoded = can_codec::EncodeLawicel(frame);
    if(!encoded || encoded->size() > max_size)
        return 0;
    memcpy(out, encoded->data(), encoded->size());
    return encoded->size();
}
