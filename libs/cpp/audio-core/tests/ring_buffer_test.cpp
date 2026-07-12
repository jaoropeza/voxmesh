#include "voxmesh/audio/ring_buffer.hpp"

#include <gtest/gtest.h>

#include <cstdint>
#include <memory>
#include <thread>
#include <vector>

namespace voxmesh::audio {
namespace {

TEST(RingBufferTest, PopOnEmptyReturnsNullopt)
{
    SpscRingBuffer<int> buffer(4);
    EXPECT_FALSE(buffer.tryPop().has_value());
    EXPECT_TRUE(buffer.empty());
    EXPECT_EQ(buffer.capacity(), 4u);
}

TEST(RingBufferTest, PreservesFifoOrder)
{
    SpscRingBuffer<int> buffer(8);
    for (int i = 0; i < 5; ++i) {
        EXPECT_TRUE(buffer.tryPush(int{i}));
    }
    for (int i = 0; i < 5; ++i) {
        const auto item = buffer.tryPop();
        ASSERT_TRUE(item.has_value());
        EXPECT_EQ(*item, i);
    }
    EXPECT_TRUE(buffer.empty());
}

TEST(RingBufferTest, RejectsWhenFullAndCountsRejections)
{
    SpscRingBuffer<int> buffer(2);
    EXPECT_TRUE(buffer.tryPush(1));
    EXPECT_TRUE(buffer.tryPush(2));
    EXPECT_FALSE(buffer.tryPush(3));
    EXPECT_FALSE(buffer.tryPush(4));

    const auto stats = buffer.stats();
    EXPECT_EQ(stats.pushed, 2u);
    EXPECT_EQ(stats.rejected, 2u);
    EXPECT_EQ(buffer.size(), 2u);
}

TEST(RingBufferTest, WrapsAroundManyTimes)
{
    SpscRingBuffer<int> buffer(3);
    for (int round = 0; round < 100; ++round) {
        EXPECT_TRUE(buffer.tryPush(int{round}));
        const auto item = buffer.tryPop();
        ASSERT_TRUE(item.has_value());
        EXPECT_EQ(*item, round);
    }
    EXPECT_EQ(buffer.stats().pushed, 100u);
    EXPECT_EQ(buffer.stats().popped, 100u);
}

TEST(RingBufferTest, SupportsMoveOnlyTypes)
{
    SpscRingBuffer<std::unique_ptr<int>> buffer(2);
    EXPECT_TRUE(buffer.tryPush(std::make_unique<int>(42)));
    auto item = buffer.tryPop();
    ASSERT_TRUE(item.has_value());
    ASSERT_TRUE(*item != nullptr);
    EXPECT_EQ(**item, 42);
}

TEST(RingBufferTest, RejectedItemIsNotConsumed)
{
    SpscRingBuffer<std::unique_ptr<int>> buffer(1);
    EXPECT_TRUE(buffer.tryPush(std::make_unique<int>(1)));
    auto second = std::make_unique<int>(2);
    EXPECT_FALSE(buffer.tryPush(std::move(second)));
    // NOLINTNEXTLINE(bugprone-use-after-move,clang-analyzer-cplusplus.Move): rejection contract keeps it valid
    ASSERT_TRUE(second != nullptr);
    EXPECT_EQ(*second, 2);
}

TEST(RingBufferTest, SingleProducerSingleConsumerStress)
{
    constexpr std::uint64_t kCount = 200000;
    SpscRingBuffer<std::uint64_t> buffer(1024);

    std::vector<std::uint64_t> received;
    received.reserve(kCount);

    std::thread consumer([&buffer, &received] {
        while (received.size() < kCount) {
            if (auto item = buffer.tryPop(); item.has_value()) {
                received.push_back(*item);
            }
        }
    });

    for (std::uint64_t i = 0; i < kCount; ++i) {
        while (!buffer.tryPush(std::uint64_t{i})) {
            // spin until the consumer catches up: nothing may be lost
        }
    }
    consumer.join();

    ASSERT_EQ(received.size(), kCount);
    for (std::uint64_t i = 0; i < kCount; ++i) {
        ASSERT_EQ(received[i], i);
    }
    EXPECT_EQ(buffer.stats().popped, kCount);
}

} // namespace
} // namespace voxmesh::audio
