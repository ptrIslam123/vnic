#include <gtest/gtest.h>

#include "error_codes.h"
#include "link/link.h"
#include "packet/packet_buffer.h"
#include "packet/packet_descriptor.h"
#include "parser/parser.h"
#include "queue/rx_queue.h"
#include "queue/tx_queue.h"
#include "utils/allocator.h"
#include "utils/fifo.h"
#include "utils/memory_pool.h"
#include "vnic/vnic.h"

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>
#include <vector>

using namespace std::chrono_literals;

namespace {

constexpr std::size_t kPayloadSize{64};

struct Segment {
    std::vector<std::byte> raw;
    vnic::PacketBuffer* buffer{};
};

Segment MakeSegment(std::size_t size) {
    Segment segment;
    segment.raw.assign(sizeof(vnic::PacketBuffer) + size, std::byte{0});
    segment.buffer         = reinterpret_cast<vnic::PacketBuffer*>(segment.raw.data());
    segment.buffer->length = static_cast<std::uint32_t>(size);
    segment.buffer->next   = nullptr;
    return segment;
}

std::vector<std::byte> Payload(std::size_t size) { return std::vector<std::byte>(size, std::byte{0xAB}); }

} // namespace

TEST(ErrorCodeTest, ValuesAreStableAndGrouped) {
    EXPECT_EQ(static_cast<std::uint32_t>(vnic::ErrorCode::None), 0u);
    EXPECT_EQ(static_cast<std::uint32_t>(vnic::ErrorCode::QueueFull), 221u);
    EXPECT_EQ(static_cast<std::uint32_t>(vnic::ErrorCode::ParseInvalidFrame), 281u);
    EXPECT_EQ(static_cast<std::uint32_t>(vnic::ErrorCode::RxInvalidQueueId), 451u);
}

TEST(ErrorCodeTest, FifoReportsFull) {
    vnic::Fifo<int> fifo;
    fifo.reserve(1);

    ASSERT_TRUE(fifo.push(1).has_value());

    auto full{fifo.push(2)};
    ASSERT_FALSE(full.has_value());
    EXPECT_EQ(full.error(), vnic::ErrorCode::QueueFull);

    const auto value = fifo.pop(1ms);
    ASSERT_TRUE(value.has_value());
    EXPECT_EQ(*value, 1);

    EXPECT_FALSE(fifo.pop(1ms).has_value()); // timeout is signalled by empty optional
}

TEST(ErrorCodeTest, LinkReportsItsOwnErrors) {
    vnic::Link link;
    vnic::Link::Config config;
    config.rxQueueDepth = 0;

    auto invalidLink{link.configure(config)};
    ASSERT_FALSE(invalidLink.has_value());
    EXPECT_EQ(invalidLink.error(), vnic::ErrorCode::ConfigLinkRxDepthInvalid);

    config              = {};
    config.rxQueueDepth = 1;
    config.txQueueDepth = 1;
    ASSERT_TRUE(link.configure(config).has_value());

    std::span<std::byte> empty;
    auto emptyFrame{link.putRx(empty)};
    ASSERT_FALSE(emptyFrame.has_value());
    EXPECT_EQ(emptyFrame.error(), vnic::ErrorCode::LinkPacketEmpty);

    auto payload = Payload(8);
    ASSERT_TRUE(link.putRx(payload).has_value());

    auto full{link.putRx(payload)};
    ASSERT_FALSE(full.has_value());
    EXPECT_EQ(full.error(), vnic::ErrorCode::LinkRxQueueFull);

    std::vector<std::byte> out;
    auto taken{link.takeRx(out, 1ms)};
    ASSERT_TRUE(taken.has_value());
    EXPECT_EQ(*taken, 8u);

    out.clear();
    auto timeout{link.takeRx(out, 1ms)};
    ASSERT_FALSE(timeout.has_value());
    EXPECT_EQ(timeout.error(), vnic::ErrorCode::Timeout);
}

TEST(ErrorCodeTest, ParserReportsMalformedFrames) {
    vnic::Parser parser;
    const std::array<std::byte, 5> shortFrame{};

    auto tooShort{parser.parse(shortFrame)};
    ASSERT_FALSE(tooShort.has_value());
    EXPECT_EQ(tooShort.error(), vnic::ErrorCode::ParseEthernetTooShort);

    const std::array<std::byte, sizeof(ethhdr)> unknownFrame{};
    auto unknown{parser.parse(unknownFrame)};
    ASSERT_FALSE(unknown.has_value());
    EXPECT_EQ(unknown.error(), vnic::ErrorCode::ParseUnsupportedEtherType);
}

TEST(ErrorCodeTest, MemoryComponentsReportConfigurationErrors) {
    vnic::memory::Allocator allocator;
    auto zero{allocator.allocate(0)};
    ASSERT_FALSE(zero.has_value());
    EXPECT_EQ(zero.error(), vnic::ErrorCode::AllocatorInvalidSize);

    auto block{allocator.allocate(8)};
    ASSERT_TRUE(block.has_value());
    allocator.deallocate(*block);
    allocator.deallocate(nullptr);

    vnic::memory::Pool<32> pool;
    vnic::memory::Pool<32>::Config poolConfig;
    auto configured{pool.configure(poolConfig)};
    ASSERT_FALSE(configured.has_value());
    EXPECT_EQ(configured.error(), vnic::ErrorCode::ConfigPoolInvalid);
}

TEST(ErrorCodeTest, StatefulObjectsReportInvalidTransitions) {
    vnic::TxQueue txQueue;
    auto started{txQueue.start()};
    ASSERT_FALSE(started.has_value());
    EXPECT_EQ(started.error(), vnic::ErrorCode::StateStartNotAllowed);

    vnic::RxQueue rxQueue;
    vnic::RxQueue::Config rxConfig;
    rxConfig.size = 0;
    auto configured{rxQueue.configure(rxConfig)};
    ASSERT_FALSE(configured.has_value());
    EXPECT_EQ(configured.error(), vnic::ErrorCode::ConfigQueueSizeInvalid);
}

TEST(ErrorCodeTest, QueueDataPathReportsReasons) {
    vnic::RxQueue rxQueue;
    vnic::PacketDescriptor desc;

    auto empty{std::span<const std::byte>{}};
    auto zeroLength{rxQueue.put(std::move(desc), empty)};
    ASSERT_FALSE(zeroLength.has_value());
    EXPECT_EQ(zeroLength.error(), vnic::ErrorCode::QueueZeroLengthPacket);

    vnic::Link link;
    vnic::TxQueue txQueue;
    ASSERT_TRUE(txQueue.configure(vnic::TxQueue::Config{}, link).has_value());

    auto nullBuffer{txQueue.put(vnic::PacketDescriptor{})};
    ASSERT_FALSE(nullBuffer.has_value());
    EXPECT_EQ(nullBuffer.error(), vnic::ErrorCode::TxDescriptorNull);

    vnic::RxQueue fullQueue;
    vnic::RxQueue::Config config;
    config.size = 1;
    ASSERT_TRUE(fullQueue.configure(config).has_value());
    ASSERT_TRUE(fullQueue.start().has_value());

    const std::array<vnic::PacketDescriptor, 1> recycled{};
    auto overfull{fullQueue.free(recycled)};
    ASSERT_FALSE(overfull.has_value());
    EXPECT_EQ(overfull.error(), vnic::ErrorCode::QueueFreeListFull);
}

TEST(ErrorCodeTest, TxQueuePropagatesLinkError) {
    vnic::Link link;
    vnic::Link::Config linkConfig;
    linkConfig.txQueueDepth = 1;
    ASSERT_TRUE(link.configure(linkConfig).has_value());

    vnic::TxQueue txQueue;
    ASSERT_TRUE(txQueue.configure(vnic::TxQueue::Config{}, link).has_value());
    ASSERT_TRUE(txQueue.start().has_value());

    Segment segment = MakeSegment(kPayloadSize);
    EXPECT_TRUE(txQueue.put(vnic::PacketDescriptor{.totalLength = kPayloadSize, .buffer = segment.buffer}).has_value());

    auto full{txQueue.put(vnic::PacketDescriptor{.totalLength = kPayloadSize, .buffer = segment.buffer})};
    ASSERT_FALSE(full.has_value());
    EXPECT_EQ(full.error(), vnic::ErrorCode::LinkTxQueueFull);

    auto mismatch{txQueue.put(vnic::PacketDescriptor{.totalLength = kPayloadSize - 1, .buffer = segment.buffer})};
    ASSERT_FALSE(mismatch.has_value());
    EXPECT_EQ(mismatch.error(), vnic::ErrorCode::TxLengthMismatch);
}

TEST(ErrorCodeTest, VnicConfigValidationReturnsSpecificCodes) {
    vnic::VNic nic;

    vnic::VNic::Config config;
    config.mtu = 0;
    auto badMtu{nic.configure(config)};
    ASSERT_FALSE(badMtu.has_value());
    EXPECT_EQ(badMtu.error(), vnic::ErrorCode::ConfigMtuInvalid);

    config         = {};
    config.macAddr = "not-a-mac";
    auto badMac{nic.configure(config)};
    ASSERT_FALSE(badMac.has_value());
    EXPECT_EQ(badMac.error(), vnic::ErrorCode::ConfigMacInvalid);

    config             = {};
    config.rss.enabled = true;
    config.rss.key     = std::vector<std::uint8_t>(39, 0);
    auto shortKey{nic.configure(config)};
    ASSERT_FALSE(shortKey.has_value());
    EXPECT_EQ(shortKey.error(), vnic::ErrorCode::ConfigRssKeyTooShort);

    config             = {};
    config.rss.enabled = true;
    config.rss.key     = std::vector<std::uint8_t>(40, 0);
    config.rss.reta    = {0, 0, 0};
    auto badRetaSize{nic.configure(config)};
    ASSERT_FALSE(badRetaSize.has_value());
    EXPECT_EQ(badRetaSize.error(), vnic::ErrorCode::ConfigRssRetaSizeNotPowerOfTwo);

    config             = {};
    config.rss.enabled = true;
    config.rss.key     = std::vector<std::uint8_t>(40, 0);
    config.rss.reta    = {1};
    auto badRetaQueue{nic.configure(config)};
    ASSERT_FALSE(badRetaQueue.has_value());
    EXPECT_EQ(badRetaQueue.error(), vnic::ErrorCode::ConfigRssRetaQueueOutOfRange);

    config               = {};
    config.vlanAllowList = {0x1000};
    auto badVlan{nic.configure(config)};
    ASSERT_FALSE(badVlan.has_value());
    EXPECT_EQ(badVlan.error(), vnic::ErrorCode::ConfigVlanInvalid);

    config                   = {};
    config.link.rxQueueDepth = 0;
    auto badLink{nic.configure(config)};
    ASSERT_FALSE(badLink.has_value());
    EXPECT_EQ(badLink.error(), vnic::ErrorCode::ConfigLinkRxDepthInvalid);

    EXPECT_TRUE(nic.configure(vnic::VNic::Config{}).has_value());
}

TEST(ErrorCodeTest, VnicControlAndDataPathErrors) {
    vnic::VNic nic;

    auto emptyReta{nic.updateReta(vnic::VNic::Config::Rss::Reta{})};
    ASSERT_FALSE(emptyReta.has_value());
    EXPECT_EQ(emptyReta.error(), vnic::ErrorCode::RssRetaEmpty);

    auto badSize{nic.updateReta(vnic::VNic::Config::Rss::Reta{0, 0, 0})};
    ASSERT_FALSE(badSize.has_value());
    EXPECT_EQ(badSize.error(), vnic::ErrorCode::RssRetaSizeInvalid);

    auto badQueue{nic.updateReta(vnic::VNic::Config::Rss::Reta{1})};
    ASSERT_FALSE(badQueue.has_value());
    EXPECT_EQ(badQueue.error(), vnic::ErrorCode::RssRetaQueueOutOfRange);

    EXPECT_TRUE(nic.updateReta(vnic::VNic::Config::Rss::Reta{0}).has_value());

    auto tx{nic.tx(0, {})};
    ASSERT_FALSE(tx.has_value());
    EXPECT_EQ(tx.error(), vnic::ErrorCode::QueueInvalidId);

    std::vector<vnic::PacketDescriptor> descs;
    auto rx{nic.rx(0, descs)};
    ASSERT_FALSE(rx.has_value());
    EXPECT_EQ(rx.error(), vnic::ErrorCode::QueueInvalidId);

    auto free{nic.freeRx(0, {})};
    ASSERT_FALSE(free.has_value());
    EXPECT_EQ(free.error(), vnic::ErrorCode::QueueInvalidId);
}
