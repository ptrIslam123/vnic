#include <gtest/gtest.h>

#include "queue/tx_queue.h"
#include "link/link.h"
#include "constanst.h"
#include "packet/packet_buffer.h"
#include "packet/packet_descriptor.h"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <utility>
#include <vector>

using namespace std::chrono_literals;

namespace {

constexpr std::byte kFill{0xAB};

// Owns the storage backing one PacketBuffer segment (header + payload).
struct Segment {
    std::vector<std::byte> raw;
    vnic::PacketBuffer* buf{};
};

Segment MakeSegment(std::size_t size, std::byte fill) {
    Segment segment;
    segment.raw.assign(sizeof(vnic::PacketBuffer) + size, std::byte{0});
    segment.buf         = reinterpret_cast<vnic::PacketBuffer*>(segment.raw.data());
    segment.buf->length = static_cast<std::uint32_t>(size);
    segment.buf->next   = nullptr;
    std::memset(segment.raw.data() + sizeof(vnic::PacketBuffer), std::to_integer<int>(fill), size);
    return segment;
}

std::vector<std::byte> Payload(std::size_t size, std::byte fill) { return std::vector<std::byte>(size, fill); }

vnic::TxQueue MakeStartedQueue(vnic::Link& link) {
    vnic::TxQueue queue;
    vnic::TxQueue::Config config;
    EXPECT_TRUE(queue.configure(config, link));
    EXPECT_TRUE(queue.start());
    return queue;
}

vnic::PacketDescriptor MakeDesc(vnic::PacketBuffer* buffer, std::uint32_t totalLength) {
    vnic::PacketDescriptor desc;
    desc.buffer      = buffer;
    desc.totalLength = totalLength;
    return desc;
}

// Reads one frame out of the link's TX pipe.
std::vector<std::byte> DrainTx(vnic::Link& link) {
    std::vector<std::byte> out;
    link.takeTx(out, 50ms);
    return out;
}

} // namespace

// ---------------------------------------------------------------------------
// Lifecycle / configuration
// ---------------------------------------------------------------------------

TEST(TxQueueTest, GetLinkIsNullBeforeConfigure) {
    vnic::TxQueue queue;

    EXPECT_EQ(queue.getLink(), nullptr);
}

TEST(TxQueueTest, ConfigureStoresLinkAndLeavesStatsZero) {
    vnic::Link link;
    vnic::TxQueue queue;
    vnic::TxQueue::Config config;

    ASSERT_TRUE(queue.configure(config, link));

    EXPECT_EQ(queue.getLink(), &link);
    EXPECT_EQ(queue.getStats().loadProcessedPackets(), 0u);
    EXPECT_EQ(queue.getStats().loadDroppedPackets(), 0u);
}

TEST(TxQueueTest, ConfigureCanBeRepeatedWithAnotherLink) {
    vnic::Link first;
    vnic::Link second;
    vnic::TxQueue queue;
    vnic::TxQueue::Config config;
    ASSERT_TRUE(queue.configure(config, first));

    ASSERT_TRUE(queue.configure(config, second));

    EXPECT_EQ(queue.getLink(), &second);
}

TEST(TxQueueTest, StartWithoutConfigureFails) {
    vnic::TxQueue queue;

    EXPECT_FALSE(queue.start());
}

TEST(TxQueueTest, StopWithoutStartFails) {
    vnic::TxQueue queue;

    EXPECT_FALSE(queue.stop());
}

TEST(TxQueueTest, StartAndStopSucceed) {
    vnic::Link link;
    vnic::TxQueue queue;
    vnic::TxQueue::Config config;
    ASSERT_TRUE(queue.configure(config, link));

    EXPECT_TRUE(queue.start());
    EXPECT_EQ(queue.getState(), vnic::TxQueue::State::Started);
    EXPECT_TRUE(queue.stop());
    EXPECT_EQ(queue.getState(), vnic::TxQueue::State::Stopped);
}

// ---------------------------------------------------------------------------
// put: success paths
// ---------------------------------------------------------------------------

TEST(TxQueueTest, PutSingleSegmentReachesLink) {
    vnic::Link link;
    vnic::TxQueue queue = MakeStartedQueue(link);

    Segment segment = MakeSegment(64, kFill);
    EXPECT_TRUE(queue.put(MakeDesc(segment.buf, 64)));

    const auto out = DrainTx(link);
    EXPECT_EQ(out, Payload(64, kFill));
    EXPECT_EQ(queue.getStats().loadProcessedPackets(), 1u);
    EXPECT_EQ(queue.getStats().loadProcessedBytes(), 64u);
}

TEST(TxQueueTest, PutMultiSegmentConcatenatesInOrder) {
    vnic::Link link;
    vnic::TxQueue queue = MakeStartedQueue(link);

    Segment first   = MakeSegment(2048, std::byte{0xAA});
    Segment second  = MakeSegment(100, std::byte{0xBB});
    first.buf->next = second.buf;

    EXPECT_TRUE(queue.put(MakeDesc(first.buf, 2148)));

    std::vector<std::byte> expected = Payload(2048, std::byte{0xAA});
    const auto tail                 = Payload(100, std::byte{0xBB});
    expected.insert(expected.end(), tail.begin(), tail.end());

    const auto out = DrainTx(link);
    EXPECT_EQ(out, expected);
    EXPECT_EQ(queue.getStats().loadProcessedBytes(), 2148u);
}

TEST(TxQueueTest, PutCopiesDataFromSourceChain) {
    vnic::Link link;
    vnic::TxQueue queue = MakeStartedQueue(link);

    Segment segment = MakeSegment(16, std::byte{0x01});
    EXPECT_TRUE(queue.put(MakeDesc(segment.buf, 16)));

    // Corrupt the source segment after the call; the link must hold a copy.
    std::memset(segment.raw.data() + sizeof(vnic::PacketBuffer), 0xFF, 16);

    const auto out = DrainTx(link);
    EXPECT_EQ(out, Payload(16, std::byte{0x01}));
}

TEST(TxQueueTest, StatsAccumulateOverMultiplePuts) {
    vnic::Link link;
    vnic::TxQueue queue = MakeStartedQueue(link);

    for (int i = 0; i < 5; ++i) {
        Segment segment = MakeSegment(32, kFill);
        ASSERT_TRUE(queue.put(MakeDesc(segment.buf, 32)));
    }

    EXPECT_EQ(queue.getStats().loadProcessedPackets(), 5u);
    EXPECT_EQ(queue.getStats().loadProcessedBytes(), 5u * 32u);

    for (int i = 0; i < 5; ++i) {
        EXPECT_EQ(DrainTx(link), Payload(32, kFill));
    }
}

// ---------------------------------------------------------------------------
// put: drop paths
// ---------------------------------------------------------------------------

TEST(TxQueueTest, PutNullBufferDropsPacket) {
    vnic::Link link;
    vnic::TxQueue queue = MakeStartedQueue(link);

    EXPECT_FALSE(queue.put(MakeDesc(nullptr, 0)));

    EXPECT_EQ(queue.getStats().loadDroppedPackets(), 1u);
    EXPECT_EQ(queue.getStats().loadDroppedBytes(), 0u);
    EXPECT_EQ(queue.getStats().loadProcessedPackets(), 0u);
    EXPECT_TRUE(DrainTx(link).empty());
}

TEST(TxQueueTest, PutWithoutConfigureDropsPacket) {
    vnic::TxQueue queue; // never configured: link_ == nullptr

    Segment segment = MakeSegment(64, kFill);
    EXPECT_FALSE(queue.put(MakeDesc(segment.buf, 64)));

    EXPECT_EQ(queue.getStats().loadDroppedPackets(), 1u);
    EXPECT_EQ(queue.getStats().loadDroppedBytes(), 64u);
    EXPECT_EQ(queue.getStats().loadProcessedPackets(), 0u);
}

TEST(TxQueueTest, PutZeroLengthPacketIsDroppedByLink) {
    vnic::Link link;
    vnic::TxQueue queue = MakeStartedQueue(link);

    Segment segment = MakeSegment(0, kFill);
    EXPECT_FALSE(queue.put(MakeDesc(segment.buf, 0)));

    EXPECT_EQ(queue.getStats().loadDroppedPackets(), 1u);
    EXPECT_EQ(queue.getStats().loadProcessedPackets(), 0u);
}

// ---------------------------------------------------------------------------
// put: chain length around MAX_SEGMENTS
// ---------------------------------------------------------------------------

TEST(TxQueueTest, PutChainAtMaxSegmentsSucceeds) {
    vnic::Link link;
    vnic::TxQueue queue = MakeStartedQueue(link);

    const std::size_t count = static_cast<std::size_t>(vnic::MAX_SEGMENTS);
    std::vector<Segment> segments(count);
    for (auto& segment : segments) {
        segment = MakeSegment(1, std::byte{0x5A});
    }
    for (std::size_t i = 0; i + 1 < segments.size(); ++i) {
        segments[i].buf->next = segments[i + 1].buf;
    }

    EXPECT_TRUE(queue.put(MakeDesc(segments.front().buf, static_cast<std::uint32_t>(count))));

    const auto out = DrainTx(link);
    EXPECT_EQ(out.size(), count);
    EXPECT_EQ(queue.getStats().loadProcessedPackets(), 1u);
}

TEST(TxQueueTest, PutChainExceedingMaxSegmentsIsDropped) {
    vnic::Link link;
    vnic::TxQueue queue = MakeStartedQueue(link);

    const std::size_t count = static_cast<std::size_t>(vnic::MAX_SEGMENTS) + 1;
    std::vector<Segment> segments(count);
    for (auto& segment : segments) {
        segment = MakeSegment(1, std::byte{0x5A});
    }
    for (std::size_t i = 0; i + 1 < segments.size(); ++i) {
        segments[i].buf->next = segments[i + 1].buf;
    }

    EXPECT_FALSE(queue.put(MakeDesc(segments.front().buf, static_cast<std::uint32_t>(count))));

    EXPECT_EQ(queue.getStats().loadDroppedPackets(), 1u);
    EXPECT_EQ(queue.getStats().loadDroppedBytes(), static_cast<std::uint64_t>(count));
    EXPECT_EQ(queue.getStats().loadProcessedPackets(), 0u);
    EXPECT_TRUE(DrainTx(link).empty());
}
