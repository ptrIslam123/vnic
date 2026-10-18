#include "vnic/vnic.h"
#include "parser/parser.h"

#include <atomic>
#include <chrono>
#include <cstdlib>
#include <csignal>
#include <cstring>
#include <iostream>
#include <span>
#include <thread>
#include <vector>
#include <random>

#include <arpa/inet.h>
#include <linux/if_ether.h>
#include <linux/ip.h>
#include <linux/udp.h>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <linux/if_ether.h>
#include <linux/ip.h>
#include <linux/udp.h>

#include "../common.h"

namespace {

std::atomic<bool> g_running{true};

void signalHandler(int) { g_running = false; }

void ProducerLoop(vnic::Link& link) {
    std::uint64_t packets{};
    std::uint64_t bytes{};
    std::vector<std::byte> buffer;
    buffer.reserve(2048);

    // Параметры для генерации
    const std::string srcMac = "bb:bb:bb:bb:bb:bb";
    const std::string dstMac = "aa:aa:aa:aa:aa:aa";
    const std::string srcIp  = "10.0.0.1";
    const std::string dstIp  = "10.0.0.2";

    std::mt19937 rng{std::random_device{}()};
    std::uniform_int_distribution<std::uint16_t> portDist{1024, 65535};
    std::uniform_int_distribution<int> sleepDist{1, 100};
    std::uniform_int_distribution<int> sizeDist{16, 512};

    while (g_running.load(std::memory_order_relaxed)) {
        const auto srcPort     = portDist(rng);
        const auto dstPort     = 53;
        const auto payloadSize = sizeDist(rng);
        const auto sleepUs     = sleepDist(rng);

        std::vector<std::byte> payload(payloadSize);
        std::memset(payload.data(), static_cast<int>(srcPort & 0xFF), payloadSize);

        buffer.clear();
        MakeUdpPacket(srcMac, dstMac, srcIp, dstIp, srcPort, dstPort, payload, buffer);

        link.putRx(buffer);
        ++packets;
        bytes += buffer.size();

        std::cout << "PRODUCER: Generated a new packet: length=" << buffer.size() << std::endl;

        // Эмуляция задержки
        std::this_thread::sleep_for(std::chrono::milliseconds{sleepUs});
    }
}

void DumpPacket(const vnic::PacketDescriptor& desc) {
    using namespace vnic;
    assert(desc.buffer);
    std::cout << desc << "\n" << *desc.buffer << std::endl;
    std::cout << "\n";
}

void ConsumerLoop(vnic::VNic& nic) {
    std::vector<vnic::PacketDescriptor> descs;
    while (g_running.load()) {
        descs.clear();
        const auto n{nic.rx(0, descs)};
        if (n > 0) {
            for (const auto& desc : descs) {
                if (desc.buffer) {
                    DumpPacket(desc);
                }
            }
            nic.freeRx(0, std::move(descs));
        } else {
            std::this_thread::sleep_for(std::chrono::milliseconds{100});
        }
    }
}

void TxProducerLoop(vnic::VNic& nic) {
    std::uint64_t packets{};
    const std::string srcMac = "cc:cc:cc:cc:cc:cc";
    const std::string dstMac = "dd:dd:dd:dd:dd:dd";
    const std::string srcIp  = "192.168.0.1";
    const std::string dstIp  = "192.168.0.2";

    std::mt19937 rng{std::random_device{}()};
    std::uniform_int_distribution<int> sleepDist{1, 100};

    while (g_running.load(std::memory_order_relaxed)) {
        std::vector<std::byte> payload(64);
        std::memset(payload.data(), 0xAB, payload.size());

        std::vector<std::byte> packet;
        packet.reserve(2048);
        MakeUdpPacket(srcMac, dstMac, srcIp, dstIp, 7777, 53, payload, packet);

        auto* buffer{static_cast<vnic::PacketBuffer*>(std::malloc(sizeof(vnic::PacketBuffer) + packet.size()))};
        if (!buffer) {
            continue;
        }
        buffer->next   = nullptr;
        buffer->length = static_cast<std::uint32_t>(packet.size());
        std::memcpy(buffer->getPacket().data(), packet.data(), packet.size());

        vnic::PacketDescriptor desc;
        desc.totalLength = static_cast<std::uint32_t>(packet.size());
        desc.buffer      = buffer;

        if (nic.tx(0, std::span{&desc, 1}) > 0) {
            ++packets;
            std::cout << "TX-PRODUCER: sent packet to queue 0: length=" << packet.size() << std::endl;
        } else {
            std::cerr << "TX-PRODUCER: dropped packet\n";
        }
        std::free(buffer);

        std::this_thread::sleep_for(std::chrono::milliseconds{sleepDist(rng)});
    }
}

void TxConsumerLoop(vnic::Link& link) {
    std::vector<std::byte> packet;
    while (g_running.load()) {
        packet.clear();
        const auto n{link.takeTx(packet, std::chrono::milliseconds{100})};
        if (n > 0) {
            std::cout << "TX-CONSUMER: received " << n << " bytes through TX path\n";
        }
    }
}

} // namespace

int main() {
    std::signal(SIGINT, signalHandler);
    std::signal(SIGTERM, signalHandler);

    std::cout << "=== VNic Example ===\n\n";

    vnic::VNic nic;

    vnic::VNic::Config config;
    config.rxQueueCount       = 4;
    config.txQueueCount       = 4;
    config.mtu                = 1500;
    config.promiscuousEnabled = true;

    config.macAddr        = "aa:bb:cc:dd:ee:ff";
    config.link.speedMbps = 1000;
    config.link.duplex    = vnic::Link::Config::Duplex::Full;

    config.rss.enabled  = true;
    config.rss.hf       = vnic::rss::HashFunc::TOEPLITZ;
    config.rss.protocol = vnic::rss::Protocol::ALL;
    config.rss.key      = {
        0x6d, 0x5a, 0x56, 0xda, 0x25, 0x5b, 0x0e, 0xc2, 0x41, 0x67, 0x25, 0x3d, 0x43, 0xa3,
        0x8f, 0xb0, 0xd0, 0xca, 0x2b, 0xcb, 0xae, 0x7b, 0x30, 0xb4, 0x77, 0xcb, 0x2d, 0xa3,
        0x80, 0x30, 0xf2, 0x0c, 0x6a, 0x42, 0xb7, 0x3b, 0xbe, 0xac, 0x01, 0xfa,
    };
    config.rss.symmetry = vnic::rss::SymmetryMode::SYMMETRIC;

    if (!nic.configure(config)) {
        std::cerr << "Failed to configure NIC\n";
        return 1;
    }

    for (std::uint16_t q = 0; q < config.rxQueueCount; ++q) {
        vnic::RxQueue::Config rxConfig;
        rxConfig.queueId  = q;
        rxConfig.size     = 512;
        rxConfig.socketId = 0;
        if (!nic.configure(q, rxConfig)) {
            std::cerr << "Failed to configure RX queue " << q << "\n";
            return 1;
        }
    }

    for (std::uint16_t q = 0; q < config.txQueueCount; ++q) {
        vnic::TxQueue::Config txConfig;
        if (!nic.configure(q, txConfig)) {
            std::cerr << "Failed to configure TX queue " << q << "\n";
            return 1;
        }
    }

    if (!nic.getLink().up()) {
        std::cerr << "Failed to bring link up\n";
        return 1;
    }

    if (!nic.start()) {
        std::cerr << "Failed to start NIC\n";
        return 1;
    }

    std::cout << "NIC started: " << config.rxQueueCount << " RX queues, "
              << "RSS " << (config.rss.enabled ? "enabled" : "disabled") << "\n";
    std::cout << "Waiting for packets... (Ctrl+C to stop)\n\n";

    std::thread consumerThread{[&] { ConsumerLoop(nic); }};
    std::thread producerThread{[&] { ProducerLoop(nic.getLink()); }};
    std::thread txProducerThread{[&] { TxProducerLoop(nic); }};
    std::thread txConsumerThread{[&] { TxConsumerLoop(nic.getLink()); }};

    producerThread.join();
    consumerThread.join();
    txProducerThread.join();
    txConsumerThread.join();

    std::cout << "\nStopping...\n";
    (void)nic.stop();

    auto stats = nic.getStats();
    std::cout << "=== Statistics ===\n";
    std::cout << "  Processed: " << stats.loadProcessedPackets() << " packets, " << stats.loadProcessedBytes()
              << " bytes\n";
    std::cout << "  Dropped:   " << stats.loadDroppedPackets() << " packets, " << stats.loadDroppedBytes()
              << " bytes\n";
    std::cout << "  TX:        " << stats.loadTransmittedPackets() << " packets, " << stats.loadTransmittedBytes()
              << " bytes\n";
    std::cout << "  TX dropped:" << stats.loadTxDroppedPackets() << " packets, " << stats.loadTxDroppedBytes()
              << " bytes\n";

    return 0;
}
