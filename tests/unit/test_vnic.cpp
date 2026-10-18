#include <gtest/gtest.h>

#include "vnic/vnic.h"
#include "constanst.h"
#include "packet/five_tuple.h"
#include "packet/packet_buffer.h"
#include "packet/packet_descriptor.h"
#include "parser/parser.h"
#include "rss/hash.h"

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <functional>
#include <optional>
#include <span>
#include <thread>
#include <utility>
#include <vector>

#include <arpa/inet.h>
#include <linux/if_ether.h>
#include <net/if_arp.h>
#include <netinet/ip.h>
#include <netinet/udp.h>

using namespace std::chrono_literals;

namespace {

constexpr std::uint8_t kSrcMac[6]{0x00, 0x11, 0x22, 0x33, 0x44, 0x55};
constexpr std::array<std::uint8_t, 6> kBroadcastMac{0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
constexpr std::array<std::uint8_t, 6> kForeignMac{0x02, 0x02, 0x02, 0x02, 0x02, 0x02};

// ---------------------------------------------------------------------------
// Frame builders (produce frames that reach VNic::validate()).
// ---------------------------------------------------------------------------

void WriteVlanTag(std::byte* at, std::uint16_t vlanId, std::uint16_t innerType) {
    const std::uint16_t tci   = htons(vlanId);
    const std::uint16_t inner = htons(innerType);
    std::memcpy(at, &tci, sizeof(tci));
    std::memcpy(at + 2, &inner, sizeof(inner));
}

void SetIpv4Checksum(std::byte* ip, bool valid) {
    auto* bytes = reinterpret_cast<std::uint8_t*>(ip);
    bytes[10]   = 0;
    bytes[11]   = 0;
    std::uint32_t sum{0};
    for (std::size_t i = 0; i + 1 < 20; i += 2) {
        sum += (static_cast<std::uint32_t>(bytes[i]) << 8) | bytes[i + 1];
    }
    while ((sum >> 16) != 0) {
        sum = (sum & 0xFFFFU) + (sum >> 16);
    }
    auto checksum = static_cast<std::uint16_t>(~sum);
    if (!valid) {
        checksum ^= 0xFFFF;
    }
    bytes[10] = static_cast<std::uint8_t>(checksum >> 8);
    bytes[11] = static_cast<std::uint8_t>(checksum & 0xFF);
}

std::vector<std::byte> MakeIpv4UdpFrame(std::size_t frameSize = 64, std::optional<std::uint16_t> vlan = std::nullopt,
                                        bool validChecksum = true, bool validTotalLength = true,
                                        const std::array<std::uint8_t, 6>& dstMac = kBroadcastMac) {
    const std::size_t l2 = sizeof(ethhdr) + (vlan ? 4 : 0);
    std::vector<std::byte> frame(frameSize, std::byte{0});

    auto* eth = reinterpret_cast<ethhdr*>(frame.data());
    std::memcpy(eth->h_dest, dstMac.data(), dstMac.size());
    std::memcpy(eth->h_source, kSrcMac, sizeof(kSrcMac));
    eth->h_proto = htons(vlan ? ETH_P_8021Q : ETH_P_IP);

    if (vlan) {
        WriteVlanTag(frame.data() + sizeof(ethhdr), *vlan, ETH_P_IP);
    }

    auto* ip     = reinterpret_cast<iphdr*>(frame.data() + l2);
    ip->version  = 4;
    ip->ihl      = 5;
    ip->ttl      = 64;
    ip->protocol = IPPROTO_UDP;
    ip->id       = htons(0x1234);
    ip->saddr    = htonl(0x0A000001);
    ip->daddr    = htonl(0x0A000002);

    const auto realLength = static_cast<std::uint16_t>(frameSize - l2);
    ip->tot_len           = htons(validTotalLength ? realLength : static_cast<std::uint16_t>(frameSize + 100));

    auto* udp   = reinterpret_cast<udphdr*>(frame.data() + l2 + sizeof(iphdr));
    udp->source = htons(12345);
    udp->dest   = htons(53);
    udp->len    = htons(static_cast<std::uint16_t>(realLength - sizeof(iphdr)));

    SetIpv4Checksum(frame.data() + l2, validChecksum);
    return frame;
}

std::vector<std::byte> MakeArpFrame(std::size_t frameSize = 64, std::optional<std::uint16_t> vlan = std::nullopt,
                                    const std::array<std::uint8_t, 6>& dstMac = kBroadcastMac) {
    const std::size_t l2 = sizeof(ethhdr) + (vlan ? 4 : 0);
    std::vector<std::byte> frame(frameSize, std::byte{0});

    auto* eth = reinterpret_cast<ethhdr*>(frame.data());
    std::memcpy(eth->h_dest, dstMac.data(), dstMac.size());
    std::memcpy(eth->h_source, kSrcMac, sizeof(kSrcMac));
    eth->h_proto = htons(vlan ? ETH_P_8021Q : ETH_P_ARP);

    if (vlan) {
        WriteVlanTag(frame.data() + sizeof(ethhdr), *vlan, ETH_P_ARP);
    }

    auto* arp   = reinterpret_cast<arphdr*>(frame.data() + l2);
    arp->ar_hrd = htons(ARPHRD_ETHER);
    arp->ar_pro = htons(ETH_P_IP);
    arp->ar_hln = 6;
    arp->ar_pln = 4;
    arp->ar_op  = htons(1); // request
    return frame;
}

// ---------------------------------------------------------------------------
// Configuration helpers
// ---------------------------------------------------------------------------

vnic::VNic::Config BaseRxConfig(std::uint16_t rxQueues = 1) {
    vnic::VNic::Config config;
    config.rxQueueCount       = rxQueues;
    config.txQueueCount       = 0;
    config.mtu                = 1500;
    config.promiscuousEnabled = true;
    config.macAddr            = "00:11:22:33:44:55";
    return config;
}

void EnableRss(vnic::VNic::Config& config, std::vector<std::uint16_t> reta = {}) {
    config.rss.enabled  = true;
    config.rss.hf       = vnic::rss::HashFunc::TOEPLITZ;
    config.rss.protocol = vnic::rss::Protocol::ALL;
    config.rss.symmetry = vnic::rss::SymmetryMode::ASYMMETRIC;
    config.rss.key      = std::vector<std::uint8_t>(40, 0x01);
    config.rss.reta     = std::move(reta);
}

bool WaitFor(const std::function<bool()>& predicate, int attempts = 200) {
    for (int i = 0; i < attempts; ++i) {
        if (predicate()) {
            return true;
        }
        std::this_thread::sleep_for(5ms);
    }
    return predicate();
}

std::vector<vnic::PacketDescriptor> WaitForRx(vnic::VNic& nic, std::uint16_t queueId, std::size_t count,
                                              int attempts = 200) {
    std::vector<vnic::PacketDescriptor> descs;
    for (int i = 0; i < attempts && descs.size() < count; ++i) {
        nic.rx(queueId, descs);
        if (descs.size() < count) {
            std::this_thread::sleep_for(5ms);
        }
    }
    return descs;
}

// ---------------------------------------------------------------------------
// TX-path helpers
// ---------------------------------------------------------------------------

struct Segment {
    std::vector<std::byte> raw;
    vnic::PacketBuffer* buffer{};
};

Segment MakeSegment(std::size_t size, std::byte fill) {
    Segment segment;
    segment.raw.assign(sizeof(vnic::PacketBuffer) + size, std::byte{0});
    segment.buffer         = reinterpret_cast<vnic::PacketBuffer*>(segment.raw.data());
    segment.buffer->length = static_cast<std::uint32_t>(size);
    segment.buffer->next   = nullptr;
    std::memset(segment.raw.data() + sizeof(vnic::PacketBuffer), std::to_integer<int>(fill), size);
    return segment;
}

std::vector<std::byte> DrainTx(vnic::VNic& nic) {
    std::vector<std::byte> out;
    (void)nic.getLink().takeTx(out, 50ms);
    return out;
}

vnic::VNic::Config BaseTxConfig(std::uint16_t txQueues = 1) {
    vnic::VNic::Config config;
    config.rxQueueCount       = 0;
    config.txQueueCount       = txQueues;
    config.promiscuousEnabled = true;
    return config;
}

} // namespace

// ===========================================================================
// RX path
// ===========================================================================

TEST(VnicRxTest, Ipv4FrameDeliveredWithHashAndMetadata) {
    vnic::VNic nic;
    vnic::VNic::Config config = BaseRxConfig(1);
    EnableRss(config);
    ASSERT_TRUE(nic.configure(config));

    vnic::RxQueue::Config queueConfig;
    queueConfig.queueId = 0;
    queueConfig.size    = 16;
    ASSERT_TRUE(nic.configure(0, queueConfig));
    ASSERT_TRUE(nic.getLink().up());
    ASSERT_TRUE(nic.start());

    auto frame = MakeIpv4UdpFrame(64);
    ASSERT_TRUE(nic.getLink().putRx(frame));

    auto descs = WaitForRx(nic, 0, 1);
    ASSERT_EQ(descs.size(), 1u);

    // The RX path must store exactly the RSS hash of the frame's 5-tuple.
    vnic::Parser parser;
    ASSERT_TRUE(parser.parse(frame));
    const auto expectedHash = vnic::rss::soft::CalcHash(vnic::Get5Tuple(parser), config.rss.key, config.rss.hf,
                                                        config.rss.protocol, config.rss.symmetry);

    EXPECT_EQ(descs[0].hash, expectedHash);
    EXPECT_EQ(descs[0].queueId, 0u);
    EXPECT_EQ(descs[0].totalLength, 64u);
    EXPECT_EQ(descs[0].vlanId, 0u);
    EXPECT_NE(descs[0].timestamp, 0u);

    EXPECT_TRUE(nic.freeRx(0, descs));
    (void)nic.stop();
}

TEST(VnicRxTest, RssDisabledHashesToZeroAndQueueZero) {
    vnic::VNic nic;
    vnic::VNic::Config config = BaseRxConfig(1); // RSS left disabled
    ASSERT_TRUE(nic.configure(config));

    vnic::RxQueue::Config queueConfig;
    queueConfig.queueId = 0;
    queueConfig.size    = 16;
    ASSERT_TRUE(nic.configure(0, queueConfig));
    ASSERT_TRUE(nic.getLink().up());
    ASSERT_TRUE(nic.start());

    auto frame = MakeIpv4UdpFrame(64);
    ASSERT_TRUE(nic.getLink().putRx(frame));

    auto descs = WaitForRx(nic, 0, 1);
    ASSERT_EQ(descs.size(), 1u);
    EXPECT_EQ(descs[0].queueId, 0u);
    EXPECT_EQ(descs[0].hash, 0u);

    (void)nic.freeRx(0, descs);
    (void)nic.stop();
}

TEST(VnicRxTest, RetaRoutesTrafficToSelectedQueue) {
    vnic::VNic nic;
    vnic::VNic::Config config = BaseRxConfig(2);
    EnableRss(config, {1, 1}); // every hash index -> queue 1
    ASSERT_TRUE(nic.configure(config));

    for (std::uint16_t q = 0; q < 2; ++q) {
        vnic::RxQueue::Config queueConfig;
        queueConfig.queueId = q;
        queueConfig.size    = 16;
        ASSERT_TRUE(nic.configure(q, queueConfig));
    }
    ASSERT_TRUE(nic.getLink().up());
    ASSERT_TRUE(nic.start());

    auto frame = MakeIpv4UdpFrame(64);
    ASSERT_TRUE(nic.getLink().putRx(frame));

    auto descs = WaitForRx(nic, 1, 1);
    ASSERT_EQ(descs.size(), 1u);
    EXPECT_EQ(descs[0].queueId, 1u);

    std::vector<vnic::PacketDescriptor> queueZero;
    nic.rx(0, queueZero);
    EXPECT_TRUE(queueZero.empty());

    (void)nic.freeRx(1, descs);
    (void)nic.stop();
}

TEST(VnicRxTest, ArpIgnoresRetaAndGoesToQueueZero) {
    vnic::VNic nic;
    vnic::VNic::Config config = BaseRxConfig(2);
    EnableRss(config, {1, 1}); // even though RETA points to queue 1...
    ASSERT_TRUE(nic.configure(config));

    for (std::uint16_t q = 0; q < 2; ++q) {
        vnic::RxQueue::Config queueConfig;
        queueConfig.queueId = q;
        queueConfig.size    = 16;
        ASSERT_TRUE(nic.configure(q, queueConfig));
    }
    ASSERT_TRUE(nic.getLink().up());
    ASSERT_TRUE(nic.start());

    auto frame = MakeArpFrame(64);
    ASSERT_TRUE(nic.getLink().putRx(frame));

    // ...ARP is non-IP, so it must land on the default queue with hash 0.
    auto descs = WaitForRx(nic, 0, 1);
    ASSERT_EQ(descs.size(), 1u);
    EXPECT_EQ(descs[0].queueId, 0u);
    EXPECT_EQ(descs[0].hash, 0u);

    (void)nic.freeRx(0, descs);
    (void)nic.stop();
}

TEST(VnicRxTest, VlanIdIsPopulatedOnDeliveredFrame) {
    vnic::VNic nic;
    vnic::VNic::Config config = BaseRxConfig(1);
    EnableRss(config);
    ASSERT_TRUE(nic.configure(config));

    vnic::RxQueue::Config queueConfig;
    queueConfig.queueId = 0;
    queueConfig.size    = 16;
    ASSERT_TRUE(nic.configure(0, queueConfig));
    ASSERT_TRUE(nic.getLink().up());
    ASSERT_TRUE(nic.start());

    auto frame = MakeIpv4UdpFrame(68, 100); // 18-byte L2 (with VLAN) + 50
    ASSERT_TRUE(nic.getLink().putRx(frame));

    auto descs = WaitForRx(nic, 0, 1);
    ASSERT_EQ(descs.size(), 1u);
    EXPECT_EQ(descs[0].vlanId, 100u);
    EXPECT_EQ(descs[0].totalLength, 68u);

    (void)nic.freeRx(0, descs);
    (void)nic.stop();
}

TEST(VnicRxTest, VlanFilterDropsDisallowedAndAcceptsAllowed) {
    vnic::VNic nic;
    vnic::VNic::Config config = BaseRxConfig(1);
    config.vlanAllowList      = {100};
    ASSERT_TRUE(nic.configure(config));

    vnic::RxQueue::Config queueConfig;
    queueConfig.queueId = 0;
    queueConfig.size    = 16;
    ASSERT_TRUE(nic.configure(0, queueConfig));
    ASSERT_TRUE(nic.getLink().up());
    ASSERT_TRUE(nic.start());

    auto dropped = MakeArpFrame(64, 200); // VLAN 200 not in the allow list
    ASSERT_TRUE(nic.getLink().putRx(dropped));
    EXPECT_TRUE(WaitFor([&] { return nic.getStats().loadDroppedPackets() > 0; }));
    EXPECT_EQ(nic.getStats().loadErrors(), 1u);

    auto allowed = MakeArpFrame(64, 100);
    ASSERT_TRUE(nic.getLink().putRx(allowed));
    auto descs = WaitForRx(nic, 0, 1);
    ASSERT_EQ(descs.size(), 1u);
    EXPECT_EQ(descs[0].vlanId, 100u);

    (void)nic.freeRx(0, descs);
    (void)nic.stop();
}

TEST(VnicRxTest, RuntFrameIsDropped) {
    vnic::VNic nic;
    vnic::VNic::Config config = BaseRxConfig(1);
    ASSERT_TRUE(nic.configure(config));

    vnic::RxQueue::Config queueConfig;
    queueConfig.queueId = 0;
    queueConfig.size    = 16;
    ASSERT_TRUE(nic.configure(0, queueConfig));
    ASSERT_TRUE(nic.getLink().up());
    ASSERT_TRUE(nic.start());

    auto runt = MakeArpFrame(40); // below MIN_FRAME_SIZE (60)
    ASSERT_TRUE(nic.getLink().putRx(runt));

    EXPECT_TRUE(WaitFor([&] { return nic.getStats().loadDroppedPackets() > 0; }));
    EXPECT_EQ(nic.getStats().loadErrors(), 1u);

    std::vector<vnic::PacketDescriptor> descs;
    nic.rx(0, descs);
    EXPECT_TRUE(descs.empty());

    (void)nic.stop();
}

TEST(VnicRxTest, ForeignDstMacDroppedWhenNotPromiscuous) {
    vnic::VNic nic;
    vnic::VNic::Config config = BaseRxConfig(1);
    config.promiscuousEnabled = false; // enforce the MAC filter
    ASSERT_TRUE(nic.configure(config));

    vnic::RxQueue::Config queueConfig;
    queueConfig.queueId = 0;
    queueConfig.size    = 16;
    ASSERT_TRUE(nic.configure(0, queueConfig));
    ASSERT_TRUE(nic.getLink().up());
    ASSERT_TRUE(nic.start());

    auto frame = MakeIpv4UdpFrame(64, std::nullopt, true, true, kForeignMac);
    ASSERT_TRUE(nic.getLink().putRx(frame));

    EXPECT_TRUE(WaitFor([&] { return nic.getStats().loadDroppedPackets() > 0; }));
    EXPECT_EQ(nic.getStats().loadErrors(), 1u);

    (void)nic.stop();
}

TEST(VnicRxTest, BadIpv4ChecksumIsDropped) {
    vnic::VNic nic;
    vnic::VNic::Config config = BaseRxConfig(1);
    ASSERT_TRUE(nic.configure(config));

    vnic::RxQueue::Config queueConfig;
    queueConfig.queueId = 0;
    queueConfig.size    = 16;
    ASSERT_TRUE(nic.configure(0, queueConfig));
    ASSERT_TRUE(nic.getLink().up());
    ASSERT_TRUE(nic.start());

    auto frame = MakeIpv4UdpFrame(64, std::nullopt, /*validChecksum=*/false);
    ASSERT_TRUE(nic.getLink().putRx(frame));

    EXPECT_TRUE(WaitFor([&] { return nic.getStats().loadDroppedPackets() > 0; }));
    EXPECT_EQ(nic.getStats().loadErrors(), 1u);

    (void)nic.stop();
}

TEST(VnicRxTest, BadIpv4TotalLengthIsDropped) {
    vnic::VNic nic;
    vnic::VNic::Config config = BaseRxConfig(1);
    ASSERT_TRUE(nic.configure(config));

    vnic::RxQueue::Config queueConfig;
    queueConfig.queueId = 0;
    queueConfig.size    = 16;
    ASSERT_TRUE(nic.configure(0, queueConfig));
    ASSERT_TRUE(nic.getLink().up());
    ASSERT_TRUE(nic.start());

    auto frame = MakeIpv4UdpFrame(64, std::nullopt, true, /*validTotalLength=*/false);
    ASSERT_TRUE(nic.getLink().putRx(frame));

    EXPECT_TRUE(WaitFor([&] { return nic.getStats().loadDroppedPackets() > 0; }));
    EXPECT_EQ(nic.getStats().loadErrors(), 1u);

    (void)nic.stop();
}

TEST(VnicRxTest, MultipleFramesAreAllDelivered) {
    vnic::VNic nic;
    vnic::VNic::Config config = BaseRxConfig(1);
    EnableRss(config);
    ASSERT_TRUE(nic.configure(config));

    vnic::RxQueue::Config queueConfig;
    queueConfig.queueId = 0;
    queueConfig.size    = 16;
    ASSERT_TRUE(nic.configure(0, queueConfig));
    ASSERT_TRUE(nic.getLink().up());
    ASSERT_TRUE(nic.start());

    constexpr int kFrames{3};
    for (int i = 0; i < kFrames; ++i) {
        auto frame = MakeIpv4UdpFrame(64);
        ASSERT_TRUE(nic.getLink().putRx(frame));
    }

    auto descs = WaitForRx(nic, 0, kFrames);
    ASSERT_EQ(descs.size(), static_cast<std::size_t>(kFrames));
    EXPECT_EQ(nic.getStats().loadProcessedPackets(), static_cast<std::uint64_t>(kFrames));

    (void)nic.freeRx(0, descs);
    (void)nic.stop();
}

TEST(VnicRxTest, InvalidQueueIdIsRejected) {
    vnic::VNic nic;
    vnic::VNic::Config config = BaseRxConfig(1);
    ASSERT_TRUE(nic.configure(config));

    vnic::RxQueue::Config queueConfig;
    queueConfig.queueId = 0;
    queueConfig.size    = 16;
    ASSERT_TRUE(nic.configure(0, queueConfig));

    std::vector<vnic::PacketDescriptor> descs;
    EXPECT_EQ(nic.rx(5, descs), -1);

    std::vector<vnic::PacketDescriptor> none;
    EXPECT_FALSE(nic.freeRx(5, none));
}

TEST(VnicRxTest, FreeRxReturnsBuffersToPool) {
    vnic::VNic nic;
    vnic::VNic::Config config = BaseRxConfig(1);
    EnableRss(config);
    ASSERT_TRUE(nic.configure(config));

    vnic::RxQueue::Config queueConfig;
    queueConfig.queueId = 0;
    queueConfig.size    = 16;
    ASSERT_TRUE(nic.configure(0, queueConfig));
    ASSERT_TRUE(nic.getLink().up());
    ASSERT_TRUE(nic.start());

    auto frame = MakeIpv4UdpFrame(64);
    ASSERT_TRUE(nic.getLink().putRx(frame));

    auto descs = WaitForRx(nic, 0, 1);
    ASSERT_EQ(descs.size(), 1u);

    const auto* queue = nic.getRxQueue(0);
    ASSERT_NE(queue, nullptr);
    EXPECT_EQ(queue->getMemPool().liveBlocks(), 1u);

    EXPECT_TRUE(nic.freeRx(0, descs));

    EXPECT_EQ(queue->getMemPool().liveBlocks(), 0u);
    (void)nic.stop();
}

TEST(VnicRxTest, UpdateRetaValidatesArguments) {
    vnic::VNic nic;
    vnic::VNic::Config config = BaseRxConfig(2);
    EnableRss(config);
    ASSERT_TRUE(nic.configure(config));

    EXPECT_FALSE(nic.updateReta(vnic::VNic::Config::Rss::Reta{}));        // empty
    EXPECT_FALSE(nic.updateReta(vnic::VNic::Config::Rss::Reta{0, 1, 0})); // not a power of two
    EXPECT_FALSE(nic.updateReta(vnic::VNic::Config::Rss::Reta{0, 2}));    // queue id out of range
    EXPECT_TRUE(nic.updateReta(vnic::VNic::Config::Rss::Reta{1, 0}));
    EXPECT_EQ(nic.getReta(), (vnic::VNic::Config::Rss::Reta{1, 0}));
}

// ===========================================================================
// TX path
// ===========================================================================

TEST(VnicTxTest, OutOfRangeQueueIdReturnsZero) {
    vnic::VNic nic;
    vnic::VNic::Config config = BaseTxConfig(0); // no TX queues
    ASSERT_TRUE(nic.configure(config));

    Segment segment = MakeSegment(64, std::byte{0xAA});
    vnic::PacketDescriptor desc;
    desc.buffer      = segment.buffer;
    desc.totalLength = 64;

    std::array<vnic::PacketDescriptor, 1> descs{desc};
    EXPECT_EQ(nic.tx(0, std::span{descs}), 0u);
}

TEST(VnicTxTest, SingleDescriptorReachesLink) {
    vnic::VNic nic;
    vnic::VNic::Config config = BaseTxConfig(1);
    ASSERT_TRUE(nic.configure(config));
    ASSERT_TRUE(nic.configure(0, vnic::TxQueue::Config{}));
    ASSERT_TRUE(nic.start());

    Segment segment = MakeSegment(64, std::byte{0xAB});
    vnic::PacketDescriptor desc;
    desc.buffer      = segment.buffer;
    desc.totalLength = 64;

    std::array<vnic::PacketDescriptor, 1> descs{desc};
    EXPECT_EQ(nic.tx(0, std::span{descs}), 1u);

    EXPECT_EQ(DrainTx(nic), std::vector<std::byte>(64, std::byte{0xAB}));
    EXPECT_EQ(nic.getStats().loadTransmittedPackets(), 1u);
    EXPECT_EQ(nic.getStats().loadTransmittedBytes(), 64u);

    (void)nic.stop();
}

TEST(VnicTxTest, MultiSegmentDescriptorIsReassembled) {
    vnic::VNic nic;
    vnic::VNic::Config config = BaseTxConfig(1);
    ASSERT_TRUE(nic.configure(config));
    ASSERT_TRUE(nic.configure(0, vnic::TxQueue::Config{}));
    ASSERT_TRUE(nic.start());

    Segment first      = MakeSegment(2048, std::byte{0x11});
    Segment second     = MakeSegment(100, std::byte{0x22});
    first.buffer->next = second.buffer;

    vnic::PacketDescriptor desc;
    desc.buffer      = first.buffer;
    desc.totalLength = 2148;

    std::array<vnic::PacketDescriptor, 1> descs{desc};
    EXPECT_EQ(nic.tx(0, std::span{descs}), 1u);

    std::vector<std::byte> expected(2048, std::byte{0x11});
    expected.insert(expected.end(), 100, std::byte{0x22});
    EXPECT_EQ(DrainTx(nic), expected);

    (void)nic.stop();
}

TEST(VnicTxTest, CounterAccumulatesOverBurst) {
    vnic::VNic nic;
    vnic::VNic::Config config = BaseTxConfig(1);
    ASSERT_TRUE(nic.configure(config));
    ASSERT_TRUE(nic.configure(0, vnic::TxQueue::Config{}));
    ASSERT_TRUE(nic.start());

    Segment a = MakeSegment(10, std::byte{0x01});
    Segment b = MakeSegment(20, std::byte{0x02});

    std::array<vnic::PacketDescriptor, 2> descs{};
    descs[0].buffer      = a.buffer;
    descs[0].totalLength = 10;
    descs[1].buffer      = b.buffer;
    descs[1].totalLength = 20;

    EXPECT_EQ(nic.tx(0, std::span{descs}), 2u);
    EXPECT_EQ(nic.getStats().loadTransmittedPackets(), 2u);
    EXPECT_EQ(nic.getStats().loadTransmittedBytes(), 30u);

    (void)nic.stop();
}

TEST(VnicTxTest, StopsAtFirstFailingDescriptor) {
    vnic::VNic nic;
    vnic::VNic::Config config = BaseTxConfig(1);
    ASSERT_TRUE(nic.configure(config));
    ASSERT_TRUE(nic.configure(0, vnic::TxQueue::Config{}));
    ASSERT_TRUE(nic.start());

    Segment a = MakeSegment(16, std::byte{0xAA});
    Segment c = MakeSegment(16, std::byte{0xCC});

    std::array<vnic::PacketDescriptor, 3> descs{};
    descs[0].buffer      = a.buffer; // ok
    descs[0].totalLength = 16;
    descs[1].buffer      = nullptr; // fails -> burst stops
    descs[1].totalLength = 0;
    descs[2].buffer      = c.buffer; // never reached
    descs[2].totalLength = 16;

    EXPECT_EQ(nic.tx(0, std::span{descs}), 1u);
    EXPECT_EQ(nic.getStats().loadTransmittedPackets(), 1u);
    EXPECT_EQ(nic.getStats().loadTxDroppedPackets(), 1u);

    // Only the first packet made it to the link.
    EXPECT_EQ(DrainTx(nic), std::vector<std::byte>(16, std::byte{0xAA}));

    (void)nic.stop();
}

TEST(VnicTxTest, NullBufferDescriptorIsCountedAsDrop) {
    vnic::VNic nic;
    vnic::VNic::Config config = BaseTxConfig(1);
    ASSERT_TRUE(nic.configure(config));
    ASSERT_TRUE(nic.configure(0, vnic::TxQueue::Config{}));
    ASSERT_TRUE(nic.start());

    vnic::PacketDescriptor desc;
    desc.buffer      = nullptr;
    desc.totalLength = 42;

    std::array<vnic::PacketDescriptor, 1> descs{desc};
    EXPECT_EQ(nic.tx(0, std::span{descs}), 0u);
    EXPECT_EQ(nic.getStats().loadTxDroppedPackets(), 1u);
    EXPECT_EQ(nic.getStats().loadTxDroppedBytes(), 42u);

    (void)nic.stop();
}

TEST(VnicTxTest, BurstCanBeSplitAcrossCalls) {
    vnic::VNic nic;
    vnic::VNic::Config config = BaseTxConfig(1);
    ASSERT_TRUE(nic.configure(config));
    ASSERT_TRUE(nic.configure(0, vnic::TxQueue::Config{}));
    ASSERT_TRUE(nic.start());

    Segment a = MakeSegment(8, std::byte{0x0A});
    Segment b = MakeSegment(8, std::byte{0x0B});

    std::array<vnic::PacketDescriptor, 1> first{};
    first[0].buffer      = a.buffer;
    first[0].totalLength = 8;
    EXPECT_EQ(nic.tx(0, std::span{first}), 1u);

    std::array<vnic::PacketDescriptor, 1> second{};
    second[0].buffer      = b.buffer;
    second[0].totalLength = 8;
    EXPECT_EQ(nic.tx(0, std::span{second}), 1u);

    std::vector<std::byte> out;
    ASSERT_EQ(nic.getLink().takeTx(out, 50ms), 8u);
    EXPECT_EQ(out, std::vector<std::byte>(8, std::byte{0x0A}));

    out.clear();
    ASSERT_EQ(nic.getLink().takeTx(out, 50ms), 8u);
    EXPECT_EQ(out, std::vector<std::byte>(8, std::byte{0x0B}));

    (void)nic.stop();
}
