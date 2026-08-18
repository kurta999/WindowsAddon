#include "TestFramework.hpp"

extern "C"
{
#include <isotp/isotp.h>
}

#include <algorithm>
#include <array>
#include <cstdarg>
#include <cstdint>
#include <vector>

namespace
{
struct CanFrame
{
    uint32_t id{};
    std::array<uint8_t, 8> data{};
    uint8_t size{};
};

struct CallbackContext
{
    std::vector<CanFrame> frames;
    uint32_t current_time_ms{};
};

std::vector<CanFrame> sent_frames;
uint32_t current_time_ms = 0;

CanFrame FrameAt(size_t index)
{
    return sent_frames.at(index);
}

void ResetTransport()
{
    sent_frames.clear();
    current_time_ms = 0;
}

int SendFromContext(void* raw_context, uint32_t arbitration_id, const uint8_t* data, uint8_t size)
{
    auto& context = *static_cast<CallbackContext*>(raw_context);
    CanFrame frame;
    frame.id = arbitration_id;
    frame.size = size;
    std::copy_n(data, size, frame.data.begin());
    context.frames.push_back(frame);
    return ISOTP_RET_OK;
}

uint32_t GetTimeFromContext(void* raw_context)
{
    return static_cast<CallbackContext*>(raw_context)->current_time_ms;
}
}

extern "C" void isotp_user_debug(const char*, ...)
{
}

extern "C" int isotp_user_send_can(const uint32_t arbitration_id, const uint8_t* data, const uint8_t size)
{
    CanFrame frame;
    frame.id = arbitration_id;
    frame.size = size;
    std::copy_n(data, size, frame.data.begin());
    sent_frames.push_back(frame);
    return ISOTP_RET_OK;
}

extern "C" uint32_t isotp_user_get_ms(void)
{
    return current_time_ms;
}

TEST_CASE(IsoTpSupportsIndependentInjectedTransportsAndClocks)
{
    ResetTransport();
    CallbackContext first_context;
    CallbackContext second_context;
    first_context.current_time_ms = 1234;
    second_context.current_time_ms = 9876;

    std::array<uint8_t, 32> first_send{};
    std::array<uint8_t, 32> first_receive{};
    std::array<uint8_t, 32> second_send{};
    std::array<uint8_t, 32> second_receive{};
    IsoTpLink first{};
    IsoTpLink second{};
    isotp_init_link(&first, 0x700, first_send.data(), static_cast<uint16_t>(first_send.size()),
                    first_receive.data(), static_cast<uint16_t>(first_receive.size()));
    isotp_init_link(&second, 0x701, second_send.data(), static_cast<uint16_t>(second_send.size()),
                    second_receive.data(), static_cast<uint16_t>(second_receive.size()));
    isotp_set_callbacks(&first, &first_context, &SendFromContext, &GetTimeFromContext);
    isotp_set_callbacks(&second, &second_context, &SendFromContext, &GetTimeFromContext);

    const std::array<uint8_t, 2> short_payload{0x10, 0x01};
    EXPECT_EQ(isotp_send(&first, short_payload.data(), static_cast<uint16_t>(short_payload.size())), ISOTP_RET_OK);
    EXPECT_EQ(isotp_send(&second, short_payload.data(), static_cast<uint16_t>(short_payload.size())), ISOTP_RET_OK);
    ASSERT_EQ(first_context.frames.size(), size_t{1});
    ASSERT_EQ(second_context.frames.size(), size_t{1});
    EXPECT_EQ(first_context.frames[0].id, uint32_t{0x700});
    EXPECT_EQ(second_context.frames[0].id, uint32_t{0x701});
    EXPECT_TRUE(sent_frames.empty());

    const std::array<uint8_t, 10> long_payload{};
    EXPECT_EQ(isotp_send(&first, long_payload.data(), static_cast<uint16_t>(long_payload.size())), ISOTP_RET_OK);
    EXPECT_EQ(first.send_timer_st, first_context.current_time_ms);
}

TEST_CASE(IsoTpSingleFrameRoundTrip)
{
    ResetTransport();
    std::array<uint8_t, 32> sender_storage{};
    std::array<uint8_t, 32> receiver_storage{};
    IsoTpLink sender{};
    IsoTpLink receiver{};
    isotp_init_link(&sender, 0x700, sender_storage.data(), static_cast<uint16_t>(sender_storage.size()),
                    receiver_storage.data(), static_cast<uint16_t>(receiver_storage.size()));

    std::array<uint8_t, 5> payload{0x11, 0x22, 0x33, 0x44, 0x55};
    EXPECT_EQ(isotp_send(&sender, payload.data(), static_cast<uint16_t>(payload.size())), ISOTP_RET_OK);
    EXPECT_EQ(sent_frames.size(), static_cast<size_t>(1));
    EXPECT_EQ(sent_frames[0].id, static_cast<uint32_t>(0x700));
    EXPECT_EQ(sent_frames[0].size, static_cast<uint8_t>(8));
    EXPECT_EQ(static_cast<unsigned>(sent_frames[0].data[0]), 0x05u);
    EXPECT_TRUE(std::equal(payload.begin(), payload.end(), sent_frames[0].data.begin() + 1));

    std::array<uint8_t, 32> receiver_send_storage{};
    isotp_init_link(&receiver, 0x701, receiver_send_storage.data(), static_cast<uint16_t>(receiver_send_storage.size()),
                    receiver_storage.data(), static_cast<uint16_t>(receiver_storage.size()));
    auto frame = FrameAt(0);
    isotp_on_can_message(&receiver, frame.data.data(), frame.size);

    std::array<uint8_t, 32> received{};
    uint16_t received_size = 0;
    EXPECT_EQ(isotp_receive(&receiver, received.data(), static_cast<uint16_t>(received.size()), &received_size), ISOTP_RET_OK);
    EXPECT_EQ(received_size, static_cast<uint16_t>(payload.size()));
    EXPECT_TRUE(std::equal(payload.begin(), payload.end(), received.begin()));
}

TEST_CASE(IsoTpMultiFrameRoundTripHonorsFlowControl)
{
    ResetTransport();
    std::array<uint8_t, 64> sender_send_storage{};
    std::array<uint8_t, 64> sender_receive_storage{};
    std::array<uint8_t, 64> receiver_send_storage{};
    std::array<uint8_t, 64> receiver_receive_storage{};
    IsoTpLink sender{};
    IsoTpLink receiver{};
    isotp_init_link(&sender, 0x700, sender_send_storage.data(), static_cast<uint16_t>(sender_send_storage.size()),
                    sender_receive_storage.data(), static_cast<uint16_t>(sender_receive_storage.size()));
    isotp_init_link(&receiver, 0x701, receiver_send_storage.data(), static_cast<uint16_t>(receiver_send_storage.size()),
                    receiver_receive_storage.data(), static_cast<uint16_t>(receiver_receive_storage.size()));

    std::array<uint8_t, 20> payload{};
    for(size_t i = 0; i < payload.size(); ++i)
        payload[i] = static_cast<uint8_t>(i);

    EXPECT_EQ(isotp_send(&sender, payload.data(), static_cast<uint16_t>(payload.size())), ISOTP_RET_OK);
    EXPECT_EQ(sent_frames.size(), static_cast<size_t>(1));
    EXPECT_EQ(static_cast<unsigned>(sent_frames[0].data[0]), 0x10u);
    EXPECT_EQ(static_cast<unsigned>(sent_frames[0].data[1]), 20u);

    auto first_frame = FrameAt(0);
    isotp_on_can_message(&receiver, first_frame.data.data(), first_frame.size);
    EXPECT_EQ(sent_frames.size(), static_cast<size_t>(2));
    EXPECT_EQ(static_cast<unsigned>(sent_frames[1].data[0]), 0x30u);

    auto flow_control = FrameAt(1);
    isotp_on_can_message(&sender, flow_control.data.data(), flow_control.size);

    isotp_poll(&sender);
    EXPECT_EQ(sent_frames.size(), static_cast<size_t>(3));
    EXPECT_EQ(static_cast<unsigned>(sent_frames[2].data[0]), 0x21u);
    auto consecutive_one = FrameAt(2);
    isotp_on_can_message(&receiver, consecutive_one.data.data(), consecutive_one.size);

    isotp_poll(&sender);
    EXPECT_EQ(sent_frames.size(), static_cast<size_t>(4));
    EXPECT_EQ(static_cast<unsigned>(sent_frames[3].data[0]), 0x22u);
    auto consecutive_two = FrameAt(3);
    isotp_on_can_message(&receiver, consecutive_two.data.data(), consecutive_two.size);

    std::array<uint8_t, 64> received{};
    uint16_t received_size = 0;
    EXPECT_EQ(isotp_receive(&receiver, received.data(), static_cast<uint16_t>(received.size()), &received_size), ISOTP_RET_OK);
    EXPECT_EQ(received_size, static_cast<uint16_t>(payload.size()));
    EXPECT_TRUE(std::equal(payload.begin(), payload.end(), received.begin()));
    EXPECT_EQ(sender.send_status, static_cast<uint8_t>(ISOTP_SEND_STATUS_IDLE));
}

TEST_CASE(IsoTpRejectsOutOfSequenceConsecutiveFrames)
{
    ResetTransport();
    std::array<uint8_t, 32> send_storage{};
    std::array<uint8_t, 32> receive_storage{};
    IsoTpLink receiver{};
    isotp_init_link(&receiver, 0x701, send_storage.data(), static_cast<uint16_t>(send_storage.size()),
                    receive_storage.data(), static_cast<uint16_t>(receive_storage.size()));

    std::array<uint8_t, 8> first_frame{0x10, 0x0A, 0, 1, 2, 3, 4, 5};
    isotp_on_can_message(&receiver, first_frame.data(), static_cast<uint8_t>(first_frame.size()));
    std::array<uint8_t, 8> wrong_sequence{0x22, 6, 7, 8, 9, 0, 0, 0};
    isotp_on_can_message(&receiver, wrong_sequence.data(), static_cast<uint8_t>(wrong_sequence.size()));

    EXPECT_EQ(receiver.receive_status, static_cast<uint8_t>(ISOTP_RECEIVE_STATUS_IDLE));
    EXPECT_EQ(receiver.receive_protocol_result, ISOTP_PROTOCOL_RESULT_WRONG_SN);
}
