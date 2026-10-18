#include <gtest/gtest.h>

#include "queue/rx_queue.h"
#include "constanst.h"
#include "packet/packet_buffer.h"
#include "packet/packet_descriptor.h"

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace {

constexpr std::uint32_t kQueueSize{4};

std::vector<std::byte> MakePacket(std::size_t size, std::byte fill = std::byte{0xAB}) {
    return std::vector<std::byte>(size, fill);
}

vnic::RxQueue MakeStartedQueue(std::uint32_t size = kQueueSize) {
    vnic::RxQueue queue;
    vnic::RxQueue::Config config;
    config.queueId = 0;
    config.size    = size;
    EXPECT_TRUE(queue.configure(config));
    EXPECT_TRUE(queue.start());
    return queue;
}

vnic::PacketDescriptor MakeDesc(std::uint16_t queueId = 0) {
    vnic::PacketDescriptor desc;
    desc.queueId = queueId;
    return desc;
}

} // namespace

// ---------------------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------------------

TEST(RxQueueTest, ConfigureStoresConfigAndLeavesFreeEmpty) {
    vnic::RxQueue queue;
    vnic::RxQueue::Config config;
    config.queueId = 2;
    config.size    = 8;

    ASSERT_TRUE(queue.configure(config));

    EXPECT_EQ(queue.getConfig().queueId, 2u);
    EXPECT_EQ(queue.getConfig().size, 8u);
    EXPECT_TRUE(queue.getFree().isEmpty());
    EXPECT_TRUE(queue.getUsed().isEmpty());
    EXPECT_EQ(queue.getStats().loadProcessedPackets(), 0u);
}

TEST(RxQueueTest, StartFillsFreeFifoWithDescriptors) {
    vnic::RxQueue queue = MakeStartedQueue();

    EXPECT_EQ(queue.getFree().size(), kQueueSize);
    EXPECT_TRUE(queue.getUsed().isEmpty());
    EXPECT_EQ(queue.getMemPool().liveBlocks(), 0u);
}

TEST(RxQueueTest, StartConfiguresMemoryPoolFromQueueSize) {
    vnic::RxQueue queue = MakeStartedQueue(16);

    const auto& memConfig = queue.getMemPool().getConfig();
    EXPECT_EQ(memConfig.capacity, 16u);
    EXPECT_EQ(memConfig.blockSize, static_cast<std::uint32_t>(vnic::PACKET_BUFFER_SIZE));
    EXPECT_EQ(memConfig.socketId, 0u);
}

TEST(RxQueueTest, StartWithoutConfigureFails) {
    vnic::RxQueue queue;

    EXPECT_FALSE(queue.start());
    EXPECT_EQ(queue.getState(), vnic::RxQueue::State::Stopped);
}

TEST(RxQueueTest, StopWithoutStartFails) {
    vnic::RxQueue queue;

    EXPECT_FALSE(queue.stop());
}

TEST(RxQueueTest, StopEmptiesBothFifos) {
    vnic::RxQueue queue = MakeStartedQueue();

    auto packet = MakePacket(64);
    ASSERT_TRUE(queue.put(MakeDesc(), packet));
    ASSERT_EQ(queue.getUsed().size(), 1u);

    ASSERT_TRUE(queue.stop());

    EXPECT_TRUE(queue.getFree().isEmpty());
    EXPECT_TRUE(queue.getUsed().isEmpty());
}

// ---------------------------------------------------------------------------
// put
// ---------------------------------------------------------------------------

TEST(RxQueueTest, PutMovesDescriptorFromFreeToUsed) {
    vnic::RxQueue queue = MakeStartedQueue();
    auto packet         = MakePacket(64);

    ASSERT_TRUE(queue.put(MakeDesc(3), packet));

    EXPECT_EQ(queue.getFree().size(), kQueueSize - 1);
    EXPECT_EQ(queue.getUsed().size(), 1u);
    EXPECT_EQ(queue.getMemPool().liveBlocks(), 1u);
    EXPECT_EQ(queue.getStats().loadProcessedPackets(), 1u);
    EXPECT_EQ(queue.getStats().loadProcessedBytes(), 64u);
}

TEST(RxQueueTest, PutOfEmptyPacketFailsWithoutSideEffects) {
    vnic::RxQueue queue = MakeStartedQueue();
    std::vector<std::byte> empty;

    EXPECT_FALSE(queue.put(MakeDesc(), empty));

    EXPECT_EQ(queue.getFree().size(), kQueueSize);
    EXPECT_TRUE(queue.getUsed().isEmpty());
    EXPECT_EQ(queue.getMemPool().liveBlocks(), 0u);
    EXPECT_EQ(queue.getStats().loadProcessedPackets(), 0u);
}

TEST(RxQueueTest, PutOverflowWhenFreeIsEmptyIncrementsOverflows) {
    vnic::RxQueue queue = MakeStartedQueue(2);
    auto packet         = MakePacket(64);

    ASSERT_TRUE(queue.put(MakeDesc(), packet));
    ASSERT_TRUE(queue.put(MakeDesc(), packet));
    EXPECT_TRUE(queue.getFree().isEmpty());
    EXPECT_EQ(queue.getMemPool().liveBlocks(), 2u);

    EXPECT_FALSE(queue.put(MakeDesc(), packet));

    EXPECT_EQ(queue.getUsed().size(), 2u);
    EXPECT_EQ(queue.getMemPool().liveBlocks(), 2u);
    EXPECT_EQ(queue.getStats().loadOverflows(), 1u);
}

TEST(RxQueueTest, PutBeyondPacketBufferSizeBuildsChain) {
    vnic::RxQueue queue       = MakeStartedQueue();
    const std::size_t bigSize = vnic::PACKET_BUFFER_SIZE + 10;
    auto packet               = MakePacket(bigSize);

    ASSERT_TRUE(queue.put(MakeDesc(), packet));

    // One descriptor consumed, but two pool blocks (segments) allocated.
    EXPECT_EQ(queue.getFree().size(), kQueueSize - 1);
    EXPECT_EQ(queue.getUsed().size(), 1u);
    EXPECT_EQ(queue.getMemPool().liveBlocks(), 2u);

    std::vector<vnic::PacketDescriptor> descs;
    ASSERT_EQ(queue.take(descs), 1u);
    ASSERT_EQ(descs.size(), 1u);
    EXPECT_EQ(descs[0].totalLength, bigSize);
    ASSERT_NE(descs[0].buffer, nullptr);
    EXPECT_EQ(descs[0].buffer->length, static_cast<std::uint32_t>(vnic::PACKET_BUFFER_SIZE));
    ASSERT_NE(descs[0].buffer->next, nullptr);
    EXPECT_EQ(descs[0].buffer->next->length, 10u);
    EXPECT_EQ(descs[0].buffer->next->next, nullptr);
}

// ---------------------------------------------------------------------------
// take
// ---------------------------------------------------------------------------

TEST(RxQueueTest, TakeReturnsZeroWhenUsedIsEmpty) {
    vnic::RxQueue queue = MakeStartedQueue();
    std::vector<vnic::PacketDescriptor> descs;

    EXPECT_EQ(queue.take(descs), 0u);
    EXPECT_TRUE(descs.empty());
    EXPECT_EQ(queue.getFree().size(), kQueueSize);
}

TEST(RxQueueTest, TakeDrainsUsedAndLeavesFreeUntouched) {
    vnic::RxQueue queue = MakeStartedQueue();
    auto packet         = MakePacket(64);
    ASSERT_TRUE(queue.put(MakeDesc(1), packet));
    ASSERT_TRUE(queue.put(MakeDesc(2), packet));
    ASSERT_EQ(queue.getFree().size(), kQueueSize - 2);

    std::vector<vnic::PacketDescriptor> descs;
    EXPECT_EQ(queue.take(descs), 2u);

    EXPECT_EQ(descs.size(), 2u);
    EXPECT_EQ(descs[0].queueId, 1);
    EXPECT_EQ(descs[1].queueId, 2);
    EXPECT_TRUE(queue.getUsed().isEmpty());
    EXPECT_EQ(queue.getFree().size(), kQueueSize - 2); // still held by the app
    EXPECT_EQ(queue.getMemPool().liveBlocks(), 2u);
}

TEST(RxQueueTest, TakeAppendsToExistingVector) {
    vnic::RxQueue queue = MakeStartedQueue();
    auto packet         = MakePacket(64);
    ASSERT_TRUE(queue.put(MakeDesc(), packet));

    std::vector<vnic::PacketDescriptor> descs(3); // pre-existing entries
    EXPECT_EQ(queue.take(descs), 1u);

    EXPECT_EQ(descs.size(), 4u);
}

TEST(RxQueueTest, TakeIsCappedAtMaxSegments) {
    constexpr std::uint32_t kDepth{600};
    vnic::RxQueue queue = MakeStartedQueue(kDepth);
    auto packet         = MakePacket(64);

    for (std::uint32_t i = 0; i < kDepth; ++i) {
        ASSERT_TRUE(queue.put(MakeDesc(), packet));
    }
    EXPECT_EQ(queue.getUsed().size(), kDepth);

    std::vector<vnic::PacketDescriptor> first;
    EXPECT_EQ(queue.take(first), vnic::MAX_SEGMENTS);
    EXPECT_EQ(first.size(), vnic::MAX_SEGMENTS);
    EXPECT_EQ(queue.getUsed().size(), kDepth - vnic::MAX_SEGMENTS);

    std::vector<vnic::PacketDescriptor> second;
    EXPECT_EQ(queue.take(second), kDepth - vnic::MAX_SEGMENTS);
    EXPECT_TRUE(queue.getUsed().isEmpty());
}

// ---------------------------------------------------------------------------
// free
// ---------------------------------------------------------------------------

TEST(RxQueueTest, FreeReturnsDescriptorsAndReleasesPool) {
    vnic::RxQueue queue = MakeStartedQueue();
    auto packet         = MakePacket(64);
    ASSERT_TRUE(queue.put(MakeDesc(), packet));

    std::vector<vnic::PacketDescriptor> descs;
    ASSERT_EQ(queue.take(descs), 1u);
    ASSERT_EQ(queue.getMemPool().liveBlocks(), 1u);

    EXPECT_TRUE(queue.free(descs));

    EXPECT_EQ(queue.getFree().size(), kQueueSize);
    EXPECT_TRUE(queue.getUsed().isEmpty());
    EXPECT_EQ(queue.getMemPool().liveBlocks(), 0u);
}

TEST(RxQueueTest, FreeEmptySpanIsNoOp) {
    vnic::RxQueue queue = MakeStartedQueue();
    std::vector<vnic::PacketDescriptor> none;

    EXPECT_TRUE(queue.free(none));

    EXPECT_EQ(queue.getFree().size(), kQueueSize);
    EXPECT_EQ(queue.getMemPool().liveBlocks(), 0u);
}

TEST(RxQueueTest, FreeMultipleDescriptorsRestoresFreeFifo) {
    vnic::RxQueue queue = MakeStartedQueue();
    auto packet         = MakePacket(64);
    for (int i = 0; i < 3; ++i) {
        ASSERT_TRUE(queue.put(MakeDesc(), packet));
    }

    std::vector<vnic::PacketDescriptor> descs;
    ASSERT_EQ(queue.take(descs), 3u);
    ASSERT_EQ(queue.getFree().size(), kQueueSize - 3);

    ASSERT_TRUE(queue.free(descs));

    EXPECT_EQ(queue.getFree().size(), kQueueSize);
    EXPECT_EQ(queue.getMemPool().liveBlocks(), 0u);
}

TEST(RxQueueTest, FreeReleasesWholeMultiSegmentChain) {
    vnic::RxQueue queue = MakeStartedQueue();
    auto packet         = MakePacket(vnic::PACKET_BUFFER_SIZE + 100);
    ASSERT_TRUE(queue.put(MakeDesc(), packet));
    ASSERT_EQ(queue.getMemPool().liveBlocks(), 2u);

    std::vector<vnic::PacketDescriptor> descs;
    ASSERT_EQ(queue.take(descs), 1u);

    ASSERT_TRUE(queue.free(descs));

    EXPECT_EQ(queue.getMemPool().liveBlocks(), 0u);
    EXPECT_EQ(queue.getFree().size(), kQueueSize);
}

// ---------------------------------------------------------------------------
// Full cycle
// ---------------------------------------------------------------------------

TEST(RxQueueTest, PutTakeFreeCycleRestoresCounts) {
    vnic::RxQueue queue = MakeStartedQueue();
    auto packet         = MakePacket(200);

    for (int i = 0; i < 10; ++i) {
        ASSERT_TRUE(queue.put(MakeDesc(static_cast<std::uint16_t>(i)), packet));
        EXPECT_EQ(queue.getUsed().size(), 1u);

        std::vector<vnic::PacketDescriptor> descs;
        ASSERT_EQ(queue.take(descs), 1u);
        ASSERT_TRUE(queue.free(descs));

        EXPECT_EQ(queue.getFree().size(), kQueueSize);
        EXPECT_TRUE(queue.getUsed().isEmpty());
        EXPECT_EQ(queue.getMemPool().liveBlocks(), 0u);
    }

    EXPECT_EQ(queue.getStats().loadProcessedPackets(), 10u);
    EXPECT_EQ(queue.getStats().loadProcessedBytes(), 10u * 200u);
}
