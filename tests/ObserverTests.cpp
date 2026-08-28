#include "TestFramework.hpp"

#include "interface/ICanSubscriber.hpp"

#include <cstdint>

namespace
{
class TestCanPublisher final : public ICanSubscriber
{
public:
    void PublishFrame()
    {
        std::uint8_t data = 42;
        NotifyFrameOnBus(0x123, &data, 1);
    }
};

class CountingObserver final : public ICanObserver
{
public:
    void OnFrameOnBus(std::uint32_t, std::uint8_t*, std::uint16_t) override
    {
        ++frame_count;
    }

    void OnIsoTpDataReceived(std::uint32_t, const std::uint8_t*, std::uint16_t) override {}

    int frame_count = 0;
};

class SelfRemovingObserver final : public ICanObserver
{
public:
    explicit SelfRemovingObserver(TestCanPublisher& publisher) : m_Publisher(publisher) {}

    void OnFrameOnBus(std::uint32_t, std::uint8_t*, std::uint16_t) override
    {
        ++frame_count;
        m_Publisher.UnregisterObserver(this);
    }

    void OnIsoTpDataReceived(std::uint32_t, const std::uint8_t*, std::uint16_t) override {}

    int frame_count = 0;

private:
    TestCanPublisher& m_Publisher;
};
}

TEST_CASE(ObserverRegistrationRejectsNullAndDuplicateObservers)
{
    TestCanPublisher publisher;
    CountingObserver observer;

    publisher.RegisterObserver(nullptr);
    publisher.RegisterObserver(&observer);
    publisher.RegisterObserver(&observer);
    publisher.PublishFrame();

    EXPECT_EQ(observer.frame_count, 1);
}

TEST_CASE(ObserverCanUnregisterDuringNotification)
{
    TestCanPublisher publisher;
    SelfRemovingObserver self_removing(publisher);
    CountingObserver persistent;
    publisher.RegisterObserver(&self_removing);
    publisher.RegisterObserver(&persistent);

    publisher.PublishFrame();
    publisher.PublishFrame();

    EXPECT_EQ(self_removing.frame_count, 1);
    EXPECT_EQ(persistent.frame_count, 2);
}
