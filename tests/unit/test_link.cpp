#include <gtest/gtest.h>

#include "link/link.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
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

} // namespace

// ---------------------------------------------------------------------------
// Configuration / state
// ---------------------------------------------------------------------------

TEST(LinkTest, DefaultLinkIsDown) {
    vnic::Link link;

    EXPECT_FALSE(link.isUp());
}

TEST(LinkTest, ConfigureStoresParameters) {
    vnic::Link link;
    vnic::Link::Config config;
    config.speedMbps = 100;
    config.duplex    = vnic::Link::Config::Duplex::Half;

    EXPECT_TRUE(link.configure(config));

    EXPECT_EQ(link.getConfig().speedMbps, 100u);
    EXPECT_EQ(link.getConfig().duplex, vnic::Link::Config::Duplex::Half);
    EXPECT_FALSE(link.isUp()); // configure does not bring the link up
}

TEST(LinkTest, UpSetsIsUp) {
    vnic::Link link;

    EXPECT_TRUE(link.up());
    EXPECT_TRUE(link.isUp());
}

TEST(LinkTest, DownClearsIsUp) {
    vnic::Link link;
    ASSERT_TRUE(link.up());

    EXPECT_TRUE(link.down());
    EXPECT_FALSE(link.isUp());
}

TEST(LinkTest, UpAndDownAreIdempotent) {
    vnic::Link link;

    EXPECT_TRUE(link.up());
    EXPECT_TRUE(link.up());
    EXPECT_TRUE(link.isUp());

    EXPECT_TRUE(link.down());
    EXPECT_TRUE(link.down());
    EXPECT_FALSE(link.isUp());
}

TEST(LinkTest, UpDownDoesNotAffectQueuedFrames) {
    vnic::Link link;
    auto payload = Payload(32);
    ASSERT_TRUE(link.putRx(payload));

    ASSERT_TRUE(link.up());
    ASSERT_TRUE(link.down());

    std::vector<std::byte> out;
    EXPECT_EQ(link.takeRx(out, kShortTimeout), 32u);
    EXPECT_EQ(out, payload);
}

// ---------------------------------------------------------------------------
// putRx / takeRx
// ---------------------------------------------------------------------------

TEST(LinkTest, PutRxRejectsEmptyPacket) {
    vnic::Link link;
    std::span<std::byte> empty;

    EXPECT_FALSE(link.putRx(empty));
}

TEST(LinkTest, PutRxTakeRxRoundTrip) {
    vnic::Link link;
    auto payload = Payload(64, std::byte{0x11});

    ASSERT_TRUE(link.putRx(payload));

    std::vector<std::byte> out;
    EXPECT_EQ(link.takeRx(out, kShortTimeout), 64u);
    EXPECT_EQ(out, payload);
}

TEST(LinkTest, PutRxCopiesTheData) {
    vnic::Link link;
    auto payload = Payload(16, std::byte{0x01});
    ASSERT_TRUE(link.putRx(payload));

    // Mutate the caller's buffer after the call; the link must hold a copy.
    std::fill(payload.begin(), payload.end(), std::byte{0xFF});

    std::vector<std::byte> out;
    EXPECT_EQ(link.takeRx(out, kShortTimeout), 16u);
    EXPECT_EQ(out, Payload(16, std::byte{0x01}));
}

TEST(LinkTest, TakeRxAppendsToOutputVector) {
    vnic::Link link;
    auto payload = Payload(8, std::byte{0x22});
    ASSERT_TRUE(link.putRx(payload));

    std::vector<std::byte> out{std::byte{0x00}};
    EXPECT_EQ(link.takeRx(out, kShortTimeout), 8u);

    std::vector<std::byte> expected{std::byte{0x00}};
    expected.insert(expected.end(), payload.begin(), payload.end());
    EXPECT_EQ(out, expected);
}

TEST(LinkTest, RxFramesAreDrainedInFifoOrder) {
    vnic::Link link;
    auto first  = Payload(4, std::byte{0x01});
    auto second = Payload(4, std::byte{0x02});
    auto third  = Payload(4, std::byte{0x03});
    ASSERT_TRUE(link.putRx(first));
    ASSERT_TRUE(link.putRx(second));
    ASSERT_TRUE(link.putRx(third));

    std::vector<std::byte> out;
    for (const std::byte fill : {std::byte{0x01}, std::byte{0x02}, std::byte{0x03}}) {
        out.clear();
        EXPECT_EQ(link.takeRx(out, kShortTimeout), 4u);
        EXPECT_EQ(out, Payload(4, fill));
    }

    out.clear();
    EXPECT_EQ(link.takeRx(out, kShortTimeout), 0u);
}

TEST(LinkTest, TakeRxOnEmptyTimesOut) {
    vnic::Link link;

    std::vector<std::byte> out{std::byte{0x00}};
    EXPECT_EQ(link.takeRx(out, kShortTimeout), 0u);
    EXPECT_EQ(out.size(), 1u); // untouched
}

TEST(LinkTest, TakeRxReturnsWhenProducerPushes) {
    vnic::Link link;
    auto payload = Payload(128, std::byte{0x33});

    std::jthread producer{[&link, &payload] {
        std::this_thread::sleep_for(10ms);
        (void)link.putRx(payload);
    }};

    std::vector<std::byte> out;
    EXPECT_EQ(link.takeRx(out, kLongTimeout), 128u);
    EXPECT_EQ(out, payload);
}

// ---------------------------------------------------------------------------
// putTx / takeTx
// ---------------------------------------------------------------------------

TEST(LinkTest, PutTxRejectsEmptyPacket) {
    vnic::Link link;
    std::span<std::byte> empty;

    EXPECT_FALSE(link.putTx(empty));
}

TEST(LinkTest, PutTxTakeTxRoundTrip) {
    vnic::Link link;
    auto payload = Payload(100, std::byte{0x44});

    ASSERT_TRUE(link.putTx(payload));

    std::vector<std::byte> out;
    EXPECT_EQ(link.takeTx(out, kShortTimeout), 100u);
    EXPECT_EQ(out, payload);
}

TEST(LinkTest, TakeTxOnEmptyTimesOut) {
    vnic::Link link;

    std::vector<std::byte> out;
    EXPECT_EQ(link.takeTx(out, kShortTimeout), 0u);
}

TEST(LinkTest, TxFramesAreDrainedInFifoOrder) {
    vnic::Link link;
    auto first  = Payload(4, std::byte{0x0A});
    auto second = Payload(4, std::byte{0x0B});
    ASSERT_TRUE(link.putTx(first));
    ASSERT_TRUE(link.putTx(second));

    std::vector<std::byte> out;
    ASSERT_EQ(link.takeTx(out, kShortTimeout), 4u);
    EXPECT_EQ(out, Payload(4, std::byte{0x0A}));

    out.clear();
    ASSERT_EQ(link.takeTx(out, kShortTimeout), 4u);
    EXPECT_EQ(out, Payload(4, std::byte{0x0B}));
}

// ---------------------------------------------------------------------------
// RX and TX pipes are independent
// ---------------------------------------------------------------------------

TEST(LinkTest, RxAndTxPipesAreIndependent) {
    vnic::Link link;
    auto rxPayload = Payload(6, std::byte{0xAA});
    auto txPayload = Payload(6, std::byte{0xBB});
    ASSERT_TRUE(link.putRx(rxPayload));
    ASSERT_TRUE(link.putTx(txPayload));

    std::vector<std::byte> txOut;
    ASSERT_EQ(link.takeTx(txOut, kShortTimeout), 6u);
    EXPECT_EQ(txOut, Payload(6, std::byte{0xBB}));

    std::vector<std::byte> rxOut;
    ASSERT_EQ(link.takeRx(rxOut, kShortTimeout), 6u);
    EXPECT_EQ(rxOut, Payload(6, std::byte{0xAA}));
}

TEST(LinkTest, LargeFrameRoundTrip) {
    vnic::Link link;
    auto payload = Payload(64 * 1024, std::byte{0x5A});

    ASSERT_TRUE(link.putRx(payload));

    std::vector<std::byte> out;
    EXPECT_EQ(link.takeRx(out, kLongTimeout), payload.size());
    EXPECT_EQ(out, payload);
}
