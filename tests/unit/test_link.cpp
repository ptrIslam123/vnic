#include <gtest/gtest.h>

#include "link/link.h"

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <span>
#include <thread>
#include <vector>

using namespace std::chrono_literals;

namespace {

std::vector<std::byte> Payload(std::size_t size, std::byte fill = std::byte{0xAB}) {
    return std::vector<std::byte>(size, fill);
}

constexpr auto kShortTimeout{20ms};
constexpr auto kLongTimeout{1s};

std::size_t TakeRx(vnic::Link& link, std::vector<std::byte>& out, std::chrono::milliseconds timeout) {
    auto taken{link.takeRx(out, timeout)};
    EXPECT_TRUE(taken.has_value());
    return taken.value_or(0);
}

std::size_t TakeTx(vnic::Link& link, std::vector<std::byte>& out, std::chrono::milliseconds timeout) {
    auto taken{link.takeTx(out, timeout)};
    EXPECT_TRUE(taken.has_value());
    return taken.value_or(0);
}

} // namespace

TEST(LinkTest, DefaultLinkIsDown) {
    vnic::Link link;

    EXPECT_FALSE(link.isUp());
}

TEST(LinkTest, ConfigureStoresQueueParameters) {
    vnic::Link link;
    vnic::Link::Config config;
    config.rxQueueDepth = 8;
    config.txQueueDepth = 16;
    config.rxFrameSize  = 128;
    config.txFrameSize  = 256;

    EXPECT_TRUE(link.configure(config).has_value());

    EXPECT_EQ(link.getConfig().rxQueueDepth, 8u);
    EXPECT_EQ(link.getConfig().txQueueDepth, 16u);
    EXPECT_EQ(link.getConfig().rxFrameSize, 128u);
    EXPECT_EQ(link.getConfig().txFrameSize, 256u);
    EXPECT_EQ(link.getConfig().rxReservedBytes(), 8u * 128u);
    EXPECT_EQ(link.getConfig().txReservedBytes(), 16u * 256u);
    EXPECT_FALSE(link.isUp()); // configure does not bring the link up
}

TEST(LinkTest, ConfigureRejectsInvalidQueueParameters) {
    vnic::Link link;
    vnic::Link::Config config;

    config.rxQueueDepth = 0;
    EXPECT_FALSE(link.configure(config).has_value());

    config              = {};
    config.txQueueDepth = 0;
    EXPECT_FALSE(link.configure(config).has_value());

    config             = {};
    config.rxFrameSize = 0;
    EXPECT_FALSE(link.configure(config).has_value());

    config             = {};
    config.txFrameSize = 0;
    EXPECT_FALSE(link.configure(config).has_value());

    EXPECT_EQ(link.getConfig().rxQueueDepth, 256u);
}

TEST(LinkTest, ConfigureLimitsQueueDepth) {
    vnic::Link link;
    vnic::Link::Config config;
    config.rxQueueDepth = 2;
    config.txQueueDepth = 1;
    config.rxFrameSize  = 64;
    config.txFrameSize  = 64;
    ASSERT_TRUE(link.configure(config).has_value());

    auto payload = Payload(8);
    EXPECT_TRUE(link.putRx(payload).has_value());
    EXPECT_TRUE(link.putRx(payload).has_value());
    EXPECT_FALSE(link.putRx(payload).has_value()); // RX pipe is full

    EXPECT_TRUE(link.putTx(payload).has_value());
    EXPECT_FALSE(link.putTx(payload).has_value()); // TX pipe is full
}

TEST(LinkTest, UpSetsIsUp) {
    vnic::Link link;

    EXPECT_TRUE(link.up().has_value());
    EXPECT_TRUE(link.isUp());
}

TEST(LinkTest, DownClearsIsUp) {
    vnic::Link link;
    ASSERT_TRUE(link.up().has_value());

    EXPECT_TRUE(link.down().has_value());
    EXPECT_FALSE(link.isUp());
}

TEST(LinkTest, UpAndDownAreIdempotent) {
    vnic::Link link;

    EXPECT_TRUE(link.up().has_value());
    EXPECT_TRUE(link.up().has_value());
    EXPECT_TRUE(link.isUp());

    EXPECT_TRUE(link.down().has_value());
    EXPECT_TRUE(link.down().has_value());
    EXPECT_FALSE(link.isUp());
}

TEST(LinkTest, QueuedFramesSurviveDownAndUp) {
    vnic::Link link;
    ASSERT_TRUE(link.up().has_value());

    auto payload = Payload(32);
    ASSERT_TRUE(link.putRx(payload).has_value());

    ASSERT_TRUE(link.down().has_value());
    EXPECT_FALSE(link.isUp());
    ASSERT_TRUE(link.up().has_value());

    std::vector<std::byte> out;
    EXPECT_EQ(TakeRx(link, out, kShortTimeout), 32u);
    EXPECT_EQ(out, payload);
}

TEST(LinkTest, FramesQueuedWhileDownAreAvailableAfterUp) {
    vnic::Link link;
    ASSERT_FALSE(link.isUp());

    auto payload = Payload(32, std::byte{0x77});
    ASSERT_TRUE(link.putRx(payload).has_value());

    ASSERT_TRUE(link.up().has_value());

    std::vector<std::byte> out;
    EXPECT_EQ(TakeRx(link, out, kShortTimeout), payload.size());
    EXPECT_EQ(out, payload);
}

TEST(LinkTest, PutRxRejectsEmptyPacket) {
    vnic::Link link;
    std::span<std::byte> empty;

    auto pushed{link.putRx(empty)};
    ASSERT_FALSE(pushed.has_value());
    EXPECT_EQ(pushed.error(), vnic::ErrorCode::LinkPacketEmpty);
}

TEST(LinkTest, PutRxRejectsWhenRxPipeIsFullAndKeepsQueuedFrames) {
    vnic::Link link;
    vnic::Link::Config config;
    config.rxQueueDepth = 1;
    ASSERT_TRUE(link.configure(config).has_value());

    auto first  = Payload(8, std::byte{0x11});
    auto second = Payload(8, std::byte{0x22});
    ASSERT_TRUE(link.putRx(first).has_value());

    auto pushed{link.putRx(second)};
    ASSERT_FALSE(pushed.has_value());
    EXPECT_EQ(pushed.error(), vnic::ErrorCode::LinkRxQueueFull);

    std::vector<std::byte> out;
    EXPECT_EQ(TakeRx(link, out, kShortTimeout), first.size());
    EXPECT_EQ(out, first);
}

TEST(LinkTest, PutRxTakeRxRoundTrip) {
    vnic::Link link;
    auto payload = Payload(64, std::byte{0x11});

    ASSERT_TRUE(link.putRx(payload).has_value());

    std::vector<std::byte> out;
    EXPECT_EQ(TakeRx(link, out, kShortTimeout), 64u);
    EXPECT_EQ(out, payload);
}

TEST(LinkTest, PutRxCopiesTheData) {
    vnic::Link link;
    auto payload = Payload(16, std::byte{0x01});
    ASSERT_TRUE(link.putRx(payload).has_value());

    // Mutate the caller's buffer after the call; the link must hold a copy.
    std::fill(payload.begin(), payload.end(), std::byte{0xFF});

    std::vector<std::byte> out;
    EXPECT_EQ(TakeRx(link, out, kShortTimeout), 16u);
    EXPECT_EQ(out, Payload(16, std::byte{0x01}));
}

TEST(LinkTest, PutRxStoresEveryByteUnchanged) {
    vnic::Link link;
    auto payload = std::vector<std::byte>{std::byte{0x00}, std::byte{0xFF}, std::byte{0x7F}, std::byte{0x80},
                                          std::byte{0x01}, std::byte{0x00}, std::byte{0xAB}};
    ASSERT_TRUE(link.putRx(payload).has_value());

    std::vector<std::byte> out;
    ASSERT_EQ(TakeRx(link, out, kShortTimeout), payload.size());
    EXPECT_EQ(out, payload);
}

TEST(LinkTest, TakeRxAppendsToOutputVector) {
    vnic::Link link;
    auto payload = Payload(8, std::byte{0x22});
    ASSERT_TRUE(link.putRx(payload).has_value());

    std::vector<std::byte> out{std::byte{0x00}};
    EXPECT_EQ(TakeRx(link, out, kShortTimeout), 8u);

    std::vector<std::byte> expected{std::byte{0x00}};
    expected.insert(expected.end(), payload.begin(), payload.end());
    EXPECT_EQ(out, expected);
}

TEST(LinkTest, RxFramesAreDrainedInFifoOrder) {
    vnic::Link link;
    auto first  = Payload(4, std::byte{0x01});
    auto second = Payload(4, std::byte{0x02});
    auto third  = Payload(4, std::byte{0x03});
    ASSERT_TRUE(link.putRx(first).has_value());
    ASSERT_TRUE(link.putRx(second).has_value());
    ASSERT_TRUE(link.putRx(third).has_value());

    std::vector<std::byte> out;
    for (const std::byte fill : {std::byte{0x01}, std::byte{0x02}, std::byte{0x03}}) {
        out.clear();
        EXPECT_EQ(TakeRx(link, out, kShortTimeout), 4u);
        EXPECT_EQ(out, Payload(4, fill));
    }

    out.clear();
    auto taken{link.takeRx(out, kShortTimeout)};
    ASSERT_FALSE(taken.has_value());
    EXPECT_EQ(taken.error(), vnic::ErrorCode::Timeout);
}

TEST(LinkTest, TakeRxOnEmptyTimesOut) {
    vnic::Link link;

    std::vector<std::byte> out{std::byte{0x00}};
    const auto started{std::chrono::steady_clock::now()};
    auto taken{link.takeRx(out, kShortTimeout)};
    const auto elapsed{std::chrono::steady_clock::now() - started};

    ASSERT_FALSE(taken.has_value());
    EXPECT_EQ(taken.error(), vnic::ErrorCode::Timeout);
    EXPECT_EQ(out.size(), 1u); // untouched
    EXPECT_GE(elapsed, kShortTimeout - 5ms);
}

TEST(LinkTest, TakeRxReturnsWhenProducerPushes) {
    vnic::Link link;
    auto payload = Payload(128, std::byte{0x33});

    std::jthread producer{[&link, &payload] {
        std::this_thread::sleep_for(10ms);
        (void)link.putRx(payload);
    }};

    std::vector<std::byte> out;
    EXPECT_EQ(TakeRx(link, out, kLongTimeout), 128u);
    EXPECT_EQ(out, payload);
}

// ---------------------------------------------------------------------------
// putTx / takeTx
// ---------------------------------------------------------------------------

TEST(LinkTest, PutTxRejectsEmptyPacket) {
    vnic::Link link;
    std::span<std::byte> empty;

    EXPECT_FALSE(link.putTx(empty).has_value());
}

TEST(LinkTest, PutTxTakeTxRoundTrip) {
    vnic::Link link;
    auto payload = Payload(100, std::byte{0x44});

    ASSERT_TRUE(link.putTx(payload).has_value());

    std::vector<std::byte> out;
    EXPECT_EQ(TakeTx(link, out, kShortTimeout), 100u);
    EXPECT_EQ(out, payload);
}

TEST(LinkTest, PutTxCopiesTheData) {
    vnic::Link link;
    auto payload = Payload(16, std::byte{0x02});
    ASSERT_TRUE(link.putTx(payload).has_value());

    std::fill(payload.begin(), payload.end(), std::byte{0xFF});

    std::vector<std::byte> out;
    EXPECT_EQ(TakeTx(link, out, kShortTimeout), 16u);
    EXPECT_EQ(out, Payload(16, std::byte{0x02}));
}

TEST(LinkTest, TakeTxAppendsToOutputVector) {
    vnic::Link link;
    auto payload = Payload(8, std::byte{0x55});
    ASSERT_TRUE(link.putTx(payload).has_value());

    std::vector<std::byte> out{std::byte{0x00}};
    EXPECT_EQ(TakeTx(link, out, kShortTimeout), payload.size());

    std::vector<std::byte> expected{std::byte{0x00}};
    expected.insert(expected.end(), payload.begin(), payload.end());
    EXPECT_EQ(out, expected);
}

TEST(LinkTest, PutTxRejectsWhenTxPipeIsFullAndKeepsQueuedFrames) {
    vnic::Link link;
    vnic::Link::Config config;
    config.txQueueDepth = 1;
    ASSERT_TRUE(link.configure(config).has_value());

    auto first  = Payload(8, std::byte{0x33});
    auto second = Payload(8, std::byte{0x44});
    ASSERT_TRUE(link.putTx(first).has_value());

    auto pushed{link.putTx(second)};
    ASSERT_FALSE(pushed.has_value());
    EXPECT_EQ(pushed.error(), vnic::ErrorCode::LinkTxQueueFull);

    std::vector<std::byte> out;
    EXPECT_EQ(TakeTx(link, out, kShortTimeout), first.size());
    EXPECT_EQ(out, first);
}

TEST(LinkTest, LargeTxFrameRoundTrip) {
    vnic::Link link;
    auto payload = Payload(64 * 1024, std::byte{0x5B});

    ASSERT_TRUE(link.putTx(payload).has_value());

    std::vector<std::byte> out;
    EXPECT_EQ(TakeTx(link, out, kLongTimeout), payload.size());
    EXPECT_EQ(out, payload);
}

TEST(LinkTest, TakeTxOnEmptyTimesOut) {
    vnic::Link link;

    std::vector<std::byte> out;
    const auto started{std::chrono::steady_clock::now()};
    auto taken{link.takeTx(out, kShortTimeout)};
    const auto elapsed{std::chrono::steady_clock::now() - started};

    ASSERT_FALSE(taken.has_value());
    EXPECT_EQ(taken.error(), vnic::ErrorCode::Timeout);
    EXPECT_TRUE(out.empty());
    EXPECT_GE(elapsed, kShortTimeout - 5ms);
}

TEST(LinkTest, TxFramesAreDrainedInFifoOrder) {
    vnic::Link link;
    auto first  = Payload(4, std::byte{0x0A});
    auto second = Payload(4, std::byte{0x0B});
    ASSERT_TRUE(link.putTx(first).has_value());
    ASSERT_TRUE(link.putTx(second).has_value());

    std::vector<std::byte> out;
    ASSERT_EQ(TakeTx(link, out, kShortTimeout), 4u);
    EXPECT_EQ(out, Payload(4, std::byte{0x0A}));

    out.clear();
    ASSERT_EQ(TakeTx(link, out, kShortTimeout), 4u);
    EXPECT_EQ(out, Payload(4, std::byte{0x0B}));
}

// ---------------------------------------------------------------------------
// RX and TX pipes are independent
// ---------------------------------------------------------------------------

TEST(LinkTest, RxAndTxPipesAreIndependent) {
    vnic::Link link;
    auto rxPayload = Payload(6, std::byte{0xAA});
    auto txPayload = Payload(6, std::byte{0xBB});
    ASSERT_TRUE(link.putRx(rxPayload).has_value());
    ASSERT_TRUE(link.putTx(txPayload).has_value());

    std::vector<std::byte> txOut;
    ASSERT_EQ(TakeTx(link, txOut, kShortTimeout), 6u);
    EXPECT_EQ(txOut, Payload(6, std::byte{0xBB}));

    std::vector<std::byte> rxOut;
    ASSERT_EQ(TakeRx(link, rxOut, kShortTimeout), 6u);
    EXPECT_EQ(rxOut, Payload(6, std::byte{0xAA}));
}

TEST(LinkTest, LargeFrameRoundTrip) {
    vnic::Link link;
    auto payload = Payload(64 * 1024, std::byte{0x5A});

    ASSERT_TRUE(link.putRx(payload).has_value());

    std::vector<std::byte> out;
    EXPECT_EQ(TakeRx(link, out, kLongTimeout), payload.size());
    EXPECT_EQ(out, payload);
}
