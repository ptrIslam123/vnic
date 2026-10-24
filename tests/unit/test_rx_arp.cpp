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

// Ethernet + ARP-запрос, дополненный до минимального размера кадра (64 байта),
// чтобы пройти проверку размера в VNic::validate.
std::vector<std::byte> MakeArpRequest() {
    constexpr std::size_t kFrameSize{64};
    std::vector<std::byte> frame(kFrameSize, std::byte{0});

    auto* eth = reinterpret_cast<ethhdr*>(frame.data());
    std::memset(eth->h_dest, 0xFF, sizeof(eth->h_dest)); // broadcast
    const std::uint8_t srcMac[6]{0x00, 0x11, 0x22, 0x33, 0x44, 0x55};
    std::memcpy(eth->h_source, srcMac, sizeof(srcMac));
    eth->h_proto = htons(ETH_P_ARP);

    auto* arp = reinterpret_cast<std::uint8_t*>(frame.data() + sizeof(ethhdr));
    arp[0]    = 0x00;
    arp[1]    = 0x01; // htype = Ethernet
    arp[2]    = 0x08;
    arp[3]    = 0x00; // ptype = IPv4
    arp[4]    = 0x06; // hlen
    arp[5]    = 0x04; // plen
    arp[6]    = 0x00;
    arp[7]    = 0x01; // oper = request

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
    config.rss.key            = std::vector<std::uint8_t>(40, 0x01);
    return config;
}

} // namespace

// ARP — не IP-трафик, у него нет 5-tuple. Как и реальный NIC, VNic не должен
// хэшировать его через RSS: кадр обязан дойти до очереди по умолчанию (0),
// а не уронить RX-поток.
TEST(VnicArp, ArpFrameGoesToDefaultQueueWithRssEnabled) {
    vnic::VNic nic;
    ASSERT_TRUE(nic.configure(MakeConfig()).has_value());

    vnic::RxQueue::Config queueConfig;
    queueConfig.queueId = 0;
    queueConfig.size    = 16;
    ASSERT_TRUE(nic.configure(0, queueConfig).has_value());

    ASSERT_TRUE(nic.getLink().up().has_value());
    ASSERT_TRUE(nic.start().has_value());

    auto frame = MakeArpRequest();
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
    EXPECT_EQ(descs[0].hash, 0u);

    EXPECT_TRUE(nic.freeRx(0, descs).has_value());
    (void)nic.stop();
}
