#include <gtest/gtest.h>

#include "packet/packet_descriptor.h"
#include "vnic/vnic.h"

#include <chrono>
#include <cstdint>
#include <cstring>
#include <thread>
#include <vector>

#include <arpa/inet.h>
#include <linux/if_ether.h>

namespace {

// Ethernet + IPv6 + UDP фиксированного размера (>= 60 байт).
std::vector<std::byte> MakeIpv6UdpPacket() {
    constexpr std::size_t kEth{14};
    constexpr std::size_t kIp6{40};
    constexpr std::size_t kUdp{8};
    constexpr std::size_t kPayload{8};

    std::vector<std::byte> frame(kEth + kIp6 + kUdp + kPayload, std::byte{0});

    auto* eth = reinterpret_cast<ethhdr*>(frame.data());
    std::memset(eth->h_dest, 0xFF, sizeof(eth->h_dest));
    const std::uint8_t srcMac[6]{0x00, 0x11, 0x22, 0x33, 0x44, 0x55};
    std::memcpy(eth->h_source, srcMac, sizeof(srcMac));
    eth->h_proto = htons(ETH_P_IPV6);

    auto* ip6             = reinterpret_cast<std::uint8_t*>(frame.data() + kEth);
    ip6[0]                = 0x60; // version = 6
    const auto payloadLen = static_cast<std::uint16_t>(kUdp + kPayload);
    ip6[4]                = static_cast<std::uint8_t>(payloadLen >> 8);
    ip6[5]                = static_cast<std::uint8_t>(payloadLen & 0xFF);
    ip6[6]                = 17; // next header = UDP
    ip6[7]                = 64; // hop limit
    ip6[8]                = 0x20;
    ip6[9]                = 0x01;
    ip6[10]               = 0x0d;
    ip6[11]               = 0xb8; // src 2001:db8::1
    ip6[23]               = 0x01;
    ip6[24]               = 0x20;
    ip6[25]               = 0x01;
    ip6[26]               = 0x0d;
    ip6[27]               = 0xb8; // dst 2001:db8::2
    ip6[39]               = 0x02;

    auto* udp = reinterpret_cast<std::uint8_t*>(frame.data() + kEth + kIp6);
    udp[0]    = 0x30;
    udp[1]    = 0x39; // sport = 12345
    udp[2]    = 0x00;
    udp[3]    = 0x35; // dport = 53
    udp[4]    = 0x00;
    udp[5]    = static_cast<std::uint8_t>(kUdp + kPayload);

    return frame;
}

vnic::VNic::Config MakeConfig() {
    vnic::VNic::Config config;
    config.rxQueueCount       = 1;
    config.txQueueCount       = 0;
    config.mtu                = 1500;
    config.promiscuousEnabled = true;
    config.macAddr            = "00:11:22:33:44:55";
    config.rss.enabled        = true;
    config.rss.hf             = vnic::rss::HashFunc::TOEPLITZ;
    config.rss.protocol       = vnic::rss::Protocol::ALL;
    config.rss.symmetry       = vnic::rss::SymmetryMode::ASYMMETRIC;
    config.rss.key            = {
        0x6d, 0x5a, 0x56, 0xda, 0x25, 0x5b, 0x0e, 0xc2, 0x41, 0x67, 0x25, 0x3d, 0x43, 0xa3,
        0x8f, 0xb0, 0xd0, 0xca, 0x2b, 0xcb, 0xae, 0x7b, 0x30, 0xb4, 0x77, 0xcb, 0x2d, 0xa3,
        0x80, 0x30, 0xf2, 0x0c, 0x6a, 0x42, 0xb7, 0x3b, 0xbe, 0xac, 0x01, 0xfa,
    };
    return config;
}

} // namespace

// IPv6-кадр должен парситься (L2=Ipv6, L3=Udp), проходить валидацию и
// распределяться с реальным RSS-хэшем (не 0, как было при отсутствии парсинга).
TEST(VnicIpv6, Ipv6UdpFrameIsParsedAndHashed) {
    vnic::VNic nic;
    ASSERT_TRUE(nic.configure(MakeConfig()).has_value());

    vnic::RxQueue::Config queueConfig;
    queueConfig.queueId = 0;
    queueConfig.size    = 16;
    ASSERT_TRUE(nic.configure(0, queueConfig).has_value());

    ASSERT_TRUE(nic.getLink().up().has_value());
    ASSERT_TRUE(nic.start().has_value());

    auto frame = MakeIpv6UdpPacket();
    ASSERT_TRUE(nic.getLink().putRx(frame).has_value());

    std::vector<vnic::PacketDescriptor> descs;
    for (int attempt = 0; attempt < 50 && descs.empty(); ++attempt) {
        (void)nic.rx(0, descs);
        if (descs.empty()) {
            std::this_thread::sleep_for(std::chrono::milliseconds{10});
        }
    }

    ASSERT_EQ(descs.size(), 1u);
    EXPECT_EQ(descs[0].queueId, 0u);
    EXPECT_NE(descs[0].hash, 0u);

    EXPECT_TRUE(nic.freeRx(0, descs).has_value());
    (void)nic.stop();
}
