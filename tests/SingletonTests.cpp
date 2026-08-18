#include "TestFramework.hpp"

#include "utils/CSingleton.hpp"

#include <thread>
#include <vector>

namespace
{
class ResettableSingleton final : public CSingleton<ResettableSingleton>
{
    friend class CSingleton<ResettableSingleton>;

public:
    int id = 0;
    inline static int construction_count = 0;
    inline static int destruction_count = 0;

private:
    ResettableSingleton() : id(++construction_count) {}
    ~ResettableSingleton() override { ++destruction_count; }
};
}

TEST_CASE(SingletonReturnsOneInstanceAndCanBeRecreatedAfterDestroy)
{
    ResettableSingleton::Destroy();
    const int constructions_before = ResettableSingleton::construction_count;
    const int destructions_before = ResettableSingleton::destruction_count;

    auto* first = ResettableSingleton::Get();
    auto* same = ResettableSingleton::Get();
    EXPECT_EQ(first, same);
    EXPECT_EQ(first->id, constructions_before + 1);

    ResettableSingleton::Destroy();
    EXPECT_EQ(ResettableSingleton::destruction_count, destructions_before + 1);

    auto* recreated = ResettableSingleton::Get();
    EXPECT_TRUE(recreated != nullptr);
    EXPECT_EQ(recreated->id, constructions_before + 2);
    ResettableSingleton::Destroy();
}

TEST_CASE(SingletonConstructionIsSerializedAcrossThreads)
{
    ResettableSingleton::Destroy();
    const int constructions_before = ResettableSingleton::construction_count;
    std::vector<ResettableSingleton*> instances(16);
    std::vector<std::thread> threads;
    threads.reserve(instances.size());

    for(std::size_t index = 0; index < instances.size(); ++index)
        threads.emplace_back([&instances, index] { instances[index] = ResettableSingleton::Get(); });
    for(auto& thread : threads)
        thread.join();

    for(auto* instance : instances)
        EXPECT_EQ(instance, instances.front());
    EXPECT_EQ(ResettableSingleton::construction_count, constructions_before + 1);
    ResettableSingleton::Destroy();
}

TEST_CASE(SingletonTryGetNeverCreatesOrResurrectsAnInstance)
{
    ResettableSingleton::Destroy();
    const int constructions_before = ResettableSingleton::construction_count;

    EXPECT_EQ(ResettableSingleton::TryGet(), nullptr);
    EXPECT_EQ(ResettableSingleton::construction_count, constructions_before);

    auto* instance = ResettableSingleton::Get();
    EXPECT_EQ(ResettableSingleton::TryGet(), instance);
    ResettableSingleton::Destroy();
    EXPECT_EQ(ResettableSingleton::TryGet(), nullptr);
}
