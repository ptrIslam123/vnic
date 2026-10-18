#include <gtest/gtest.h>

#include "utils/fifo.h"

#include <array>
#include <chrono>
#include <cstddef>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <thread>
#include <type_traits>
#include <utility>
#include <vector>

using namespace std::chrono_literals;

namespace {

// A small move-only type used to exercise the move paths of the FIFO.
struct MoveOnly {
    int value{0};

    MoveOnly() = default;
    explicit MoveOnly(int v) : value{v} {}
    MoveOnly(const MoveOnly&)                = delete;
    MoveOnly& operator=(const MoveOnly&)     = delete;
    MoveOnly(MoveOnly&&) noexcept            = default;
    MoveOnly& operator=(MoveOnly&&) noexcept = default;
};

} // namespace

// ---------------------------------------------------------------------------
// Construction / emptiness
// ---------------------------------------------------------------------------

TEST(FifoTest, DefaultConstructedIsEmpty) {
    vnic::Fifo<int> fifo;

    EXPECT_TRUE(fifo.isEmpty());
    EXPECT_EQ(fifo.size(), 0u);
}

TEST(FifoTest, IsEmptyAndSizeReflectState) {
    vnic::Fifo<int> fifo;

    EXPECT_TRUE(fifo.isEmpty());

    fifo.push(1);
    EXPECT_FALSE(fifo.isEmpty());
    EXPECT_EQ(fifo.size(), 1u);

    fifo.push(2);
    fifo.push(3);
    EXPECT_EQ(fifo.size(), 3u);

    (void)fifo.pop();
    EXPECT_EQ(fifo.size(), 2u);

    (void)fifo.pop();
    (void)fifo.pop();
    EXPECT_TRUE(fifo.isEmpty());
    EXPECT_EQ(fifo.size(), 0u);
}

TEST(FifoTest, ReserveIsCallableAndDoesNotChangeSize) {
    vnic::Fifo<int> fifo;

    fifo.reserve(128);

    EXPECT_EQ(fifo.size(), 0u);
    EXPECT_TRUE(fifo.isEmpty());
}

// ---------------------------------------------------------------------------
// push
// ---------------------------------------------------------------------------

TEST(FifoTest, PushCopyReturnsTrueAndStoresElement) {
    vnic::Fifo<int> fifo;
    const int value = 42;

    EXPECT_TRUE(fifo.push(value));
    EXPECT_EQ(fifo.size(), 1u);
    EXPECT_EQ(fifo.pop(), 42);
}

TEST(FifoTest, PushMoveReturnsTrueAndStoresElement) {
    vnic::Fifo<std::string> fifo;
    std::string value = "hello";

    EXPECT_TRUE(fifo.push(std::move(value)));
    EXPECT_EQ(fifo.size(), 1u);
    EXPECT_EQ(fifo.pop(), "hello");
}

// ---------------------------------------------------------------------------
// emplace
// ---------------------------------------------------------------------------

TEST(FifoTest, EmplaceConstructsElementInPlace) {
    vnic::Fifo<int> fifo;

    EXPECT_TRUE(fifo.emplace(7));
    EXPECT_EQ(fifo.size(), 1u);
    EXPECT_EQ(fifo.pop(), 7);
}

TEST(FifoTest, EmplaceForwardsMultipleArguments) {
    vnic::Fifo<std::string> fifo;

    EXPECT_TRUE(fifo.emplace(3, 'x')); // std::string(3, 'x') == "xxx"
    EXPECT_EQ(fifo.pop(), "xxx");
}

TEST(FifoTest, EmplaceConstructsMoveOnlyElement) {
    vnic::Fifo<std::unique_ptr<int>> fifo;

    EXPECT_TRUE(fifo.emplace(std::make_unique<int>(11)));
    auto value = fifo.pop();
    ASSERT_NE(value, nullptr);
    EXPECT_EQ(*value, 11);
}

// ---------------------------------------------------------------------------
// pushAll / pushSome
// ---------------------------------------------------------------------------

TEST(FifoTest, PushAllStoresEveryElementInOrder) {
    vnic::Fifo<int> fifo;
    const std::array<int, 3> data{1, 2, 3};

    fifo.pushAll(data);

    EXPECT_EQ(fifo.size(), 3u);
    EXPECT_EQ(fifo.pop(), 1);
    EXPECT_EQ(fifo.pop(), 2);
    EXPECT_EQ(fifo.pop(), 3);
}

TEST(FifoTest, PushAllWithEmptySpanDoesNothing) {
    vnic::Fifo<int> fifo;
    const std::span<const int> empty;

    fifo.pushAll(empty);

    EXPECT_TRUE(fifo.isEmpty());
}

TEST(FifoTest, PushSomeReturnsNumberOfElementsPushed) {
    vnic::Fifo<int> fifo;
    const std::array<int, 4> data{10, 20, 30, 40};

    EXPECT_EQ(fifo.pushSome(data), 4u);
    EXPECT_EQ(fifo.size(), 4u);
    EXPECT_EQ(fifo.pop(), 10);
    EXPECT_EQ(fifo.pop(), 20);
    EXPECT_EQ(fifo.pop(), 30);
    EXPECT_EQ(fifo.pop(), 40);
}

TEST(FifoTest, PushSomeWithEmptySpanReturnsZero) {
    vnic::Fifo<int> fifo;
    const std::span<const int> empty;

    EXPECT_EQ(fifo.pushSome(empty), 0u);
    EXPECT_TRUE(fifo.isEmpty());
}

// ---------------------------------------------------------------------------
// pop
// ---------------------------------------------------------------------------

TEST(FifoTest, PopBlockingReturnsElementsInFifoOrder) {
    vnic::Fifo<int> fifo;
    fifo.push(1);
    fifo.push(2);
    fifo.push(3);

    EXPECT_EQ(fifo.pop(), 1);
    EXPECT_EQ(fifo.pop(), 2);
    EXPECT_EQ(fifo.pop(), 3);
}

TEST(FifoTest, PopWithTimeoutReturnsElementWhenAvailable) {
    vnic::Fifo<int> fifo;
    fifo.push(99);

    const std::optional<int> value = fifo.pop(50ms);

    ASSERT_TRUE(value.has_value());
    EXPECT_EQ(*value, 99);
    EXPECT_TRUE(fifo.isEmpty());
}

TEST(FifoTest, PopWithTimeoutReturnsNulloptWhenEmpty) {
    vnic::Fifo<int> fifo;

    const std::optional<int> value = fifo.pop(20ms);

    EXPECT_FALSE(value.has_value());
}

TEST(FifoTest, PopWakesBlockedConsumer) {
    vnic::Fifo<int> fifo;
    int received{-1};

    std::jthread consumer{[&fifo, &received] { received = fifo.pop(); }};

    std::this_thread::sleep_for(10ms);
    EXPECT_TRUE(fifo.push(123));

    consumer.join();
    EXPECT_EQ(received, 123);
}

TEST(FifoTest, PopWithTimeoutWakesOnPush) {
    vnic::Fifo<int> fifo;
    std::optional<int> received;

    std::jthread consumer{[&fifo, &received] { received = fifo.pop(1s); }};

    std::this_thread::sleep_for(10ms);
    EXPECT_TRUE(fifo.push(321));

    consumer.join();
    ASSERT_TRUE(received.has_value());
    EXPECT_EQ(*received, 321);
}

// ---------------------------------------------------------------------------
// popSome(vector)
// ---------------------------------------------------------------------------

TEST(FifoTest, PopSomeVectorDrainsWholeQueueAndAppends) {
    vnic::Fifo<int> fifo;
    fifo.push(1);
    fifo.push(2);
    std::vector<int> buffer{100}; // pre-existing content must be preserved

    const std::size_t count = fifo.popSome(buffer);

    EXPECT_EQ(count, 2u);
    ASSERT_EQ(buffer.size(), 3u);
    EXPECT_EQ(buffer[0], 100);
    EXPECT_EQ(buffer[1], 1);
    EXPECT_EQ(buffer[2], 2);
    EXPECT_TRUE(fifo.isEmpty());
}

TEST(FifoTest, PopSomeVectorWithTimeoutReturnsElements) {
    vnic::Fifo<int> fifo;
    fifo.push(5);
    fifo.push(6);
    std::vector<int> buffer;

    const std::size_t count = fifo.popSome(buffer, 50ms);

    EXPECT_EQ(count, 2u);
    ASSERT_EQ(buffer.size(), 2u);
    EXPECT_EQ(buffer[0], 5);
    EXPECT_EQ(buffer[1], 6);
}

TEST(FifoTest, PopSomeVectorWithTimeoutReturnsZeroWhenEmpty) {
    vnic::Fifo<int> fifo;
    std::vector<int> buffer;

    const std::size_t count = fifo.popSome(buffer, 20ms);

    EXPECT_EQ(count, 0u);
    EXPECT_TRUE(buffer.empty());
}

// ---------------------------------------------------------------------------
// popSome(span<T, N>)
// ---------------------------------------------------------------------------

TEST(FifoTest, PopSomeSpanBlockingPopsUpToN) {
    vnic::Fifo<int> fifo;
    for (int i = 0; i < 5; ++i) {
        fifo.push(i);
    }

    std::array<int, 3> buffer{};
    const std::size_t count = fifo.popSome(std::span{buffer});

    EXPECT_EQ(count, 3u);
    EXPECT_EQ(buffer[0], 0);
    EXPECT_EQ(buffer[1], 1);
    EXPECT_EQ(buffer[2], 2);
    EXPECT_EQ(fifo.size(), 2u);
}

TEST(FifoTest, PopSomeSpanWithTimeoutReturnsElements) {
    vnic::Fifo<int> fifo;
    fifo.push(8);
    fifo.push(9);

    std::array<int, 4> buffer{};
    const std::size_t count = fifo.popSome(std::span{buffer}, 50ms);

    EXPECT_EQ(count, 2u);
    EXPECT_EQ(buffer[0], 8);
    EXPECT_EQ(buffer[1], 9);
}

TEST(FifoTest, PopSomeSpanWithTimeoutReturnsZeroWhenEmpty) {
    vnic::Fifo<int> fifo;

    std::array<int, 4> buffer{};
    const std::size_t count = fifo.popSome(std::span{buffer}, 20ms);

    EXPECT_EQ(count, 0u);
}

// ---------------------------------------------------------------------------
// tryPopSome
// ---------------------------------------------------------------------------

TEST(FifoTest, TryPopSomeReturnsZeroWhenEmpty) {
    vnic::Fifo<int> fifo;

    std::array<int, 4> buffer{};
    EXPECT_EQ(fifo.tryPopSome(std::span{buffer}), 0u);
}

TEST(FifoTest, TryPopSomePopsUpToNWithoutBlocking) {
    vnic::Fifo<int> fifo;
    for (int i = 0; i < 4; ++i) {
        fifo.push(i);
    }

    std::array<int, 2> buffer{};
    const std::size_t count = fifo.tryPopSome(std::span{buffer});

    EXPECT_EQ(count, 2u);
    EXPECT_EQ(buffer[0], 0);
    EXPECT_EQ(buffer[1], 1);
    EXPECT_EQ(fifo.size(), 2u);
}

// ---------------------------------------------------------------------------
// Move semantics
// ---------------------------------------------------------------------------

TEST(FifoTest, MoveConstructTransfersElements) {
    vnic::Fifo<int> source;
    source.push(1);
    source.push(2);

    vnic::Fifo<int> moved{std::move(source)};

    EXPECT_EQ(moved.size(), 2u);
    EXPECT_EQ(moved.pop(), 1);
    EXPECT_EQ(moved.pop(), 2);
}

TEST(FifoTest, MoveAssignmentTransfersElements) {
    vnic::Fifo<int> source;
    source.push(41);
    source.push(42);

    vnic::Fifo<int> target;
    target = std::move(source);

    EXPECT_EQ(target.size(), 2u);
    EXPECT_EQ(target.pop(), 41);
    EXPECT_EQ(target.pop(), 42);
}

TEST(FifoTest, MovedToFifoIsReusable) {
    vnic::Fifo<int> source;
    source.push(1);

    vnic::Fifo<int> moved{std::move(source)};

    EXPECT_TRUE(moved.push(2));
    EXPECT_EQ(moved.size(), 2u);
    EXPECT_EQ(moved.pop(), 1);
    EXPECT_EQ(moved.pop(), 2);
}

// ---------------------------------------------------------------------------
// Type properties / element types
// ---------------------------------------------------------------------------

TEST(FifoTest, IsMoveOnly) {
    static_assert(!std::is_copy_constructible_v<vnic::Fifo<int>>);
    static_assert(!std::is_copy_assignable_v<vnic::Fifo<int>>);
    static_assert(std::is_move_constructible_v<vnic::Fifo<int>>);
    static_assert(std::is_move_assignable_v<vnic::Fifo<int>>);
    SUCCEED();
}

TEST(FifoTest, SupportsMoveOnlyElementType) {
    vnic::Fifo<MoveOnly> fifo;

    EXPECT_TRUE(fifo.emplace(5));
    EXPECT_EQ(fifo.pop().value, 5);

    EXPECT_TRUE(fifo.push(MoveOnly{6}));
    EXPECT_EQ(fifo.pop().value, 6);

    fifo.push(MoveOnly{7});
    fifo.push(MoveOnly{8});
    std::array<MoveOnly, 4> buffer{};
    EXPECT_EQ(fifo.popSome(std::span{buffer}), 2u);
    EXPECT_EQ(buffer[0].value, 7);
    EXPECT_EQ(buffer[1].value, 8);
}

// ---------------------------------------------------------------------------
// Thread safety
// ---------------------------------------------------------------------------

TEST(FifoTest, ProducerConsumerAcrossThreadsPreservesAllElements) {
    constexpr int kCount = 1000;

    vnic::Fifo<int> fifo;
    std::thread producer{[&fifo] {
        for (int i = 0; i < kCount; ++i) {
            fifo.push(i);
        }
    }};

    long long sum = 0;
    for (int i = 0; i < kCount; ++i) {
        sum += fifo.pop();
    }

    producer.join();

    constexpr long long kExpected = static_cast<long long>(kCount) * (kCount - 1) / 2;
    EXPECT_EQ(sum, kExpected);
    EXPECT_TRUE(fifo.isEmpty());
}
