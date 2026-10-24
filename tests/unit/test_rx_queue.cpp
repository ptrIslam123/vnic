#include <gtest/gtest.h>

#include "constants.h"
#include "packet/packet_buffer.h"
#include "packet/packet_descriptor.h"
#include "queue/rx_queue.h"

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace {

constexpr std::uint32_t kQueueSize{4};

std::vector<std::byte> MakePacket(std::size_t size, std::byte fill = std::byte{0xAB}) {
    return std::vector<std::byte>(size, fill);
}

std::vector<std::byte> CopyPacket(const vnic::PacketBuffer* head) {
    std::vector<std::byte> bytes;
    for (const vnic::PacketBuffer* segment = head; segment != nullptr; segment = segment->next) {
        const auto payload = segment->getPacket();
        bytes.insert(bytes.end(), payload.begin(), payload.end());
    }
    return bytes;
}

vnic::RxQueue MakeStartedQueue(std::uint32_t size = kQueueSize) {
    vnic::RxQueue queue;
    vnic::RxQueue::Config config;
    config.queueId = 0;
    config.size    = size;
    EXPECT_TRUE(queue.configure(config).has_value());
    EXPECT_TRUE(queue.start().has_value());
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

    ASSERT_TRUE(queue.configure(config).has_value());

    EXPECT_EQ(queue.getConfig().queueId, 2u);
    EXPECT_EQ(queue.getConfig().size, 8u);
    EXPECT_TRUE(queue.getFree().isEmpty());
    EXPECT_TRUE(queue.getUsed().isEmpty());
    EXPECT_EQ(queue.getStats().loadProcessedPackets(), 0u);
}

TEST(RxQueueTest, DefaultConfigIsValid) {
    const vnic::RxQueue::Config config;

    EXPECT_TRUE(config.isValid());
    EXPECT_EQ(config.queueId, 0u);
    EXPECT_EQ(config.size, 512u);
}

TEST(RxQueueTest, ConfigureAcceptsExplicitlyValidConfigurations) {
    for (const std::uint32_t size : {1u, 4u, 512u, 4096u}) {
        SCOPED_TRACE(size);
        vnic::RxQueue queue;
        vnic::RxQueue::Config config;
        config.queueId  = 2;
        config.size     = size;
        config.socketId = 3;

        EXPECT_TRUE(config.isValid());
        ASSERT_TRUE(queue.configure(config).has_value());
        EXPECT_EQ(queue.getState(), vnic::RxQueue::State::Configured);
        EXPECT_EQ(queue.getConfig().queueId, 2u);
        EXPECT_EQ(queue.getConfig().size, size);
        EXPECT_EQ(queue.getConfig().socketId, 3u);
    }
}

TEST(RxQueueTest, ConfigureRejectsAllZeroSizedConfigurations) {
    for (const std::uint32_t queueId : {0u, 7u}) {
        for (const std::uint32_t socketId : {0u, 3u}) {
            SCOPED_TRACE(queueId);
            SCOPED_TRACE(socketId);
            vnic::RxQueue queue;
            vnic::RxQueue::Config config;
            config.queueId  = static_cast<std::uint16_t>(queueId);
            config.size     = 0;
            config.socketId = socketId;

            EXPECT_FALSE(config.isValid());
            auto configured{queue.configure(config)};
            ASSERT_FALSE(configured.has_value());
            EXPECT_EQ(configured.error(), vnic::ErrorCode::ConfigQueueSizeInvalid);
            EXPECT_EQ(queue.getState(), vnic::RxQueue::State::Error);
        }
    }
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
    EXPECT_EQ(memConfig.blockSize, static_cast<std::uint32_t>(vnic::kPacketBufferSize));
    EXPECT_EQ(memConfig.socketId, 0u);
}

TEST(RxQueueTest, ConfigurePropagatesSocketIdToPoolAndFifos) {
    vnic::RxQueue queue;
    vnic::RxQueue::Config config;
    config.size     = 8;
    config.socketId = 3;

    ASSERT_TRUE(queue.configure(config).has_value());

    EXPECT_EQ(queue.getMemPool().getConfig().socketId, 3u);
    EXPECT_EQ(queue.getFree().socketId(), 3u);
    EXPECT_EQ(queue.getUsed().socketId(), 3u);
}

TEST(RxQueueTest, StartWithoutExplicitConfigureUsesDefaultConfig) {
    vnic::RxQueue queue;

    ASSERT_TRUE(queue.start().has_value());

    EXPECT_EQ(queue.getState(), vnic::RxQueue::State::Started);
    EXPECT_EQ(queue.getConfig().size, vnic::RxQueue::Config{}.size);
    EXPECT_EQ(queue.getFree().size(), vnic::RxQueue::Config{}.size);
    EXPECT_TRUE(queue.getUsed().isEmpty());
    EXPECT_EQ(queue.getMemPool().liveBlocks(), 0u);
    EXPECT_TRUE(queue.stop().has_value());
}

TEST(RxQueueTest, StopWithoutStartFails) {
    vnic::RxQueue queue;

    EXPECT_FALSE(queue.stop().has_value());
}

TEST(RxQueueTest, StopEmptiesBothFifos) {
    vnic::RxQueue queue = MakeStartedQueue();

    auto packet = MakePacket(64);
    ASSERT_TRUE(queue.put(MakeDesc(), packet).has_value());
    ASSERT_EQ(queue.getUsed().size(), 1u);

    ASSERT_TRUE(queue.stop().has_value());

    EXPECT_TRUE(queue.getFree().isEmpty());
    EXPECT_TRUE(queue.getUsed().isEmpty());
}

// ---------------------------------------------------------------------------
// put
// ---------------------------------------------------------------------------

TEST(RxQueueTest, PutMovesDescriptorFromFreeToUsed) {
    vnic::RxQueue queue = MakeStartedQueue();
    auto packet         = MakePacket(64);

    ASSERT_TRUE(queue.put(MakeDesc(3), packet).has_value());

    EXPECT_EQ(queue.getFree().size(), kQueueSize - 1);
    EXPECT_EQ(queue.getUsed().size(), 1u);
    EXPECT_EQ(queue.getMemPool().liveBlocks(), 1u);
    EXPECT_EQ(queue.getStats().loadProcessedPackets(), 1u);
    EXPECT_EQ(queue.getStats().loadProcessedBytes(), 64u);
}

TEST(RxQueueTest, PutOfEmptyPacketFailsWithoutSideEffects) {
    vnic::RxQueue queue = MakeStartedQueue();
    std::vector<std::byte> empty;

    EXPECT_FALSE(queue.put(MakeDesc(), empty).has_value());

    EXPECT_EQ(queue.getFree().size(), kQueueSize);
    EXPECT_TRUE(queue.getUsed().isEmpty());
    EXPECT_EQ(queue.getMemPool().liveBlocks(), 0u);
    EXPECT_EQ(queue.getStats().loadProcessedPackets(), 0u);
}

TEST(RxQueueTest, PutOverflowWhenFreeIsEmptyIncrementsOverflows) {
    vnic::RxQueue queue = MakeStartedQueue(2);
    auto packet         = MakePacket(64);

    ASSERT_TRUE(queue.put(MakeDesc(), packet).has_value());
    ASSERT_TRUE(queue.put(MakeDesc(), packet).has_value());
    EXPECT_TRUE(queue.getFree().isEmpty());
    EXPECT_EQ(queue.getMemPool().liveBlocks(), 2u);

    EXPECT_FALSE(queue.put(MakeDesc(), packet).has_value());

    EXPECT_EQ(queue.getUsed().size(), 2u);
    EXPECT_EQ(queue.getMemPool().liveBlocks(), 2u);
    EXPECT_EQ(queue.getStats().loadOverflows(), 1u);
}

TEST(RxQueueTest, PutBeyondPacketBufferSizeBuildsChain) {
    vnic::RxQueue queue       = MakeStartedQueue();
    const std::size_t bigSize = vnic::kPacketBufferSize + 10;
    auto packet               = MakePacket(bigSize);

    ASSERT_TRUE(queue.put(MakeDesc(), packet).has_value());

    // One descriptor consumed, but two pool blocks (segments) allocated.
    EXPECT_EQ(queue.getFree().size(), kQueueSize - 1);
    EXPECT_EQ(queue.getUsed().size(), 1u);
    EXPECT_EQ(queue.getMemPool().liveBlocks(), 2u);

    std::vector<vnic::PacketDescriptor> descs;
    ASSERT_EQ(queue.take(descs), 1u);
    ASSERT_EQ(descs.size(), 1u);
    EXPECT_EQ(descs[0].totalLength, bigSize);
    ASSERT_NE(descs[0].buffer, nullptr);
    EXPECT_EQ(descs[0].buffer->length, static_cast<std::uint32_t>(vnic::kPacketBufferSize));
    ASSERT_NE(descs[0].buffer->next, nullptr);
    EXPECT_EQ(descs[0].buffer->next->length, 10u);
    EXPECT_EQ(descs[0].buffer->next->next, nullptr);

    ASSERT_TRUE(queue.free(descs).has_value());
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
    ASSERT_TRUE(queue.put(MakeDesc(1), packet).has_value());
    ASSERT_TRUE(queue.put(MakeDesc(2), packet).has_value());
    ASSERT_EQ(queue.getFree().size(), kQueueSize - 2);

    std::vector<vnic::PacketDescriptor> descs;
    EXPECT_EQ(queue.take(descs), 2u);

    EXPECT_EQ(descs.size(), 2u);
    EXPECT_EQ(descs[0].queueId, 1);
    EXPECT_EQ(descs[1].queueId, 2);
    EXPECT_TRUE(queue.getUsed().isEmpty());
    EXPECT_EQ(queue.getFree().size(), kQueueSize - 2); // still held by the app
    EXPECT_EQ(queue.getMemPool().liveBlocks(), 2u);

    ASSERT_TRUE(queue.free(descs).has_value());
}

TEST(RxQueueTest, TakeAppendsToExistingVector) {
    vnic::RxQueue queue = MakeStartedQueue();
    auto packet         = MakePacket(64);
    ASSERT_TRUE(queue.put(MakeDesc(), packet).has_value());

    std::vector<vnic::PacketDescriptor> descs(3); // pre-existing entries
    EXPECT_EQ(queue.take(descs), 1u);

    EXPECT_EQ(descs.size(), 4u);

    const std::span<const vnic::PacketDescriptor> received{descs.data() + descs.size() - 1, 1};
    ASSERT_TRUE(queue.free(received).has_value());
}

TEST(RxQueueTest, TakeIsCappedAtMaxSegments) {
    constexpr std::uint32_t kDepth{600};
    vnic::RxQueue queue = MakeStartedQueue(kDepth);
    auto packet         = MakePacket(64);

    for (std::uint32_t i = 0; i < kDepth; ++i) {
        ASSERT_TRUE(queue.put(MakeDesc(), packet).has_value());
    }
    EXPECT_EQ(queue.getUsed().size(), kDepth);

    std::vector<vnic::PacketDescriptor> first;
    EXPECT_EQ(queue.take(first), vnic::kMaxSegments);
    EXPECT_EQ(first.size(), vnic::kMaxSegments);
    EXPECT_EQ(queue.getUsed().size(), kDepth - vnic::kMaxSegments);

    std::vector<vnic::PacketDescriptor> second;
    EXPECT_EQ(queue.take(second), kDepth - vnic::kMaxSegments);
    EXPECT_TRUE(queue.getUsed().isEmpty());

    EXPECT_TRUE(queue.free(first).has_value());
    EXPECT_TRUE(queue.free(second).has_value());
}

// ---------------------------------------------------------------------------
// free
// ---------------------------------------------------------------------------

TEST(RxQueueTest, FreeReturnsDescriptorsAndReleasesPool) {
    vnic::RxQueue queue = MakeStartedQueue();
    auto packet         = MakePacket(64);
    ASSERT_TRUE(queue.put(MakeDesc(), packet).has_value());

    std::vector<vnic::PacketDescriptor> descs;
    ASSERT_EQ(queue.take(descs), 1u);
    ASSERT_EQ(queue.getMemPool().liveBlocks(), 1u);

    EXPECT_TRUE(queue.free(descs).has_value());

    EXPECT_EQ(queue.getFree().size(), kQueueSize);
    EXPECT_TRUE(queue.getUsed().isEmpty());
    EXPECT_EQ(queue.getMemPool().liveBlocks(), 0u);
}

TEST(RxQueueTest, FreeEmptySpanIsNoOp) {
    vnic::RxQueue queue = MakeStartedQueue();
    std::vector<vnic::PacketDescriptor> none;

    EXPECT_TRUE(queue.free(none).has_value());

    EXPECT_EQ(queue.getFree().size(), kQueueSize);
    EXPECT_EQ(queue.getMemPool().liveBlocks(), 0u);
}

TEST(RxQueueTest, FreeMultipleDescriptorsRestoresFreeFifo) {
    vnic::RxQueue queue = MakeStartedQueue();
    auto packet         = MakePacket(64);
    for (int i = 0; i < 3; ++i) {
        ASSERT_TRUE(queue.put(MakeDesc(), packet).has_value());
    }

    std::vector<vnic::PacketDescriptor> descs;
    ASSERT_EQ(queue.take(descs), 3u);
    ASSERT_EQ(queue.getFree().size(), kQueueSize - 3);

    ASSERT_TRUE(queue.free(descs).has_value());

    EXPECT_EQ(queue.getFree().size(), kQueueSize);
    EXPECT_EQ(queue.getMemPool().liveBlocks(), 0u);
}

TEST(RxQueueTest, FreeReleasesWholeMultiSegmentChain) {
    vnic::RxQueue queue = MakeStartedQueue();
    auto packet         = MakePacket(vnic::kPacketBufferSize + 100);
    ASSERT_TRUE(queue.put(MakeDesc(), packet).has_value());
    ASSERT_EQ(queue.getMemPool().liveBlocks(), 2u);

    std::vector<vnic::PacketDescriptor> descs;
    ASSERT_EQ(queue.take(descs), 1u);

    ASSERT_TRUE(queue.free(descs).has_value());

    EXPECT_EQ(queue.getMemPool().liveBlocks(), 0u);
    EXPECT_EQ(queue.getFree().size(), kQueueSize);
}

// ---------------------------------------------------------------------------
// Full cycle
// ---------------------------------------------------------------------------

TEST(RxQueueTest, PutTakeFreeRoundTripPreservesPacketsAndRestoresPool) {
    vnic::RxQueue queue   = MakeStartedQueue();
    const auto packetA    = MakePacket(64, std::byte{0x11});
    const auto packetB    = MakePacket(vnic::kPacketBufferSize + 17, std::byte{0x22});
    const auto packetC    = MakePacket(1, std::byte{0x33});
    const auto freeBefore = queue.getFree().size();
    const auto liveBefore = queue.getMemPool().liveBlocks();
    ASSERT_EQ(freeBefore, kQueueSize);
    ASSERT_EQ(liveBefore, 0u);

    ASSERT_TRUE(queue.put(MakeDesc(11), packetA).has_value());
    ASSERT_TRUE(queue.put(MakeDesc(12), packetB).has_value());
    ASSERT_TRUE(queue.put(MakeDesc(13), packetC).has_value());

    EXPECT_EQ(queue.getFree().size(), freeBefore - 3);
    EXPECT_EQ(queue.getUsed().size(), 3u);
    EXPECT_EQ(queue.getMemPool().liveBlocks(), 4u); // 1 + 2 + 1 segments

    std::vector<vnic::PacketDescriptor> descs;
    ASSERT_EQ(queue.take(descs), 3u);
    ASSERT_EQ(descs.size(), 3u);

    EXPECT_EQ(descs[0].queueId, 11u);
    EXPECT_EQ(descs[1].queueId, 12u);
    EXPECT_EQ(descs[2].queueId, 13u);
    EXPECT_EQ(descs[0].totalLength, packetA.size());
    EXPECT_EQ(descs[1].totalLength, packetB.size());
    EXPECT_EQ(descs[2].totalLength, packetC.size());
    EXPECT_EQ(CopyPacket(descs[0].buffer), packetA);
    EXPECT_EQ(CopyPacket(descs[1].buffer), packetB);
    EXPECT_EQ(CopyPacket(descs[2].buffer), packetC);

    EXPECT_TRUE(queue.getUsed().isEmpty());
    EXPECT_EQ(queue.getMemPool().liveBlocks(), 4u);

    ASSERT_TRUE(queue.free(descs).has_value());

    EXPECT_EQ(queue.getFree().size(), freeBefore);
    EXPECT_TRUE(queue.getUsed().isEmpty());
    EXPECT_EQ(queue.getMemPool().liveBlocks(), liveBefore);
    EXPECT_EQ(queue.getStats().loadProcessedPackets(), 3u);
    EXPECT_EQ(queue.getStats().loadProcessedBytes(), packetA.size() + packetB.size() + packetC.size());
}

TEST(RxQueueTest, PutBeyondSegmentLimitIncrementsOverflowsOnly) {
    vnic::RxQueue queue        = MakeStartedQueue();
    const std::size_t oversize = static_cast<std::size_t>(vnic::kMaxSegments + 1) * vnic::kPacketBufferSize;
    auto packet                = MakePacket(oversize);

    auto put{queue.put(MakeDesc(), packet)};
    ASSERT_FALSE(put.has_value());
    EXPECT_EQ(put.error(), vnic::ErrorCode::QueueSegmentLimitExceeded);
    EXPECT_EQ(queue.getStats().loadOverflows(), 1u);
    EXPECT_EQ(queue.getStats().loadProcessedPackets(), 0u);
    EXPECT_EQ(queue.getStats().loadProcessedBytes(), 0u);
    EXPECT_EQ(queue.getStats().loadDroppedPackets(), 0u);
    EXPECT_EQ(queue.getStats().loadDroppedBytes(), 0u);
    EXPECT_EQ(queue.getStats().loadErrors(), 0u);
    EXPECT_EQ(queue.getMemPool().liveBlocks(), 0u);
    EXPECT_EQ(queue.getMemPool().totalBlocks(), 0u);
    EXPECT_EQ(queue.getFree().size(), kQueueSize);
}

TEST(RxQueueTest, StatsDoNotTouchUnrelatedCounters) {
    vnic::RxQueue queue = MakeStartedQueue();
    auto packet         = MakePacket(64);

    ASSERT_TRUE(queue.put(MakeDesc(), packet).has_value());
    EXPECT_EQ(queue.getStats().loadProcessedPackets(), 1u);
    EXPECT_EQ(queue.getStats().loadProcessedBytes(), 64u);

    std::vector<vnic::PacketDescriptor> descs;
    ASSERT_EQ(queue.take(descs), 1u);
    ASSERT_TRUE(queue.free(descs).has_value());

    EXPECT_EQ(queue.getStats().loadDroppedPackets(), 0u);
    EXPECT_EQ(queue.getStats().loadDroppedBytes(), 0u);
    EXPECT_EQ(queue.getStats().loadErrors(), 0u);
    EXPECT_EQ(queue.getStats().loadOverflows(), 0u);
    EXPECT_EQ(queue.getStats().loadTransmittedPackets(), 0u);
    EXPECT_EQ(queue.getStats().loadTransmittedBytes(), 0u);
    EXPECT_EQ(queue.getStats().loadTxDroppedPackets(), 0u);
    EXPECT_EQ(queue.getStats().loadTxDroppedBytes(), 0u);
}

TEST(RxQueueTest, PutTakeFreeCycleRestoresCounts) {
    vnic::RxQueue queue = MakeStartedQueue();
    auto packet         = MakePacket(200);

    for (int i = 0; i < 10; ++i) {
        ASSERT_TRUE(queue.put(MakeDesc(static_cast<std::uint16_t>(i)), packet).has_value());
        EXPECT_EQ(queue.getUsed().size(), 1u);

        std::vector<vnic::PacketDescriptor> descs;
        ASSERT_EQ(queue.take(descs), 1u);
        ASSERT_TRUE(queue.free(descs).has_value());

        EXPECT_EQ(queue.getFree().size(), kQueueSize);
        EXPECT_TRUE(queue.getUsed().isEmpty());
        EXPECT_EQ(queue.getMemPool().liveBlocks(), 0u);
    }

    EXPECT_EQ(queue.getStats().loadProcessedPackets(), 10u);
    EXPECT_EQ(queue.getStats().loadProcessedBytes(), 10u * 200u);
}
