#include "vnic/vnic.h"

#include <array>
#include <vector>
#include <thread>
#include <chrono>
#include <cstring>
#include <iostream>
#include <iomanip>

#include <string>
#include <vector>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <stdexcept>
#include <arpa/inet.h>

#include <linux/if_ether.h>
#include <linux/ip.h>
#include <linux/udp.h>

static std::array<std::uint8_t, 6> parseMac(const std::string& mac) {
    std::array<std::uint8_t, 6> result{};
    unsigned int bytes[6];

    if (std::sscanf(mac.c_str(), "%x:%x:%x:%x:%x:%x",
                    &bytes[0], &bytes[1], &bytes[2],
                    &bytes[3], &bytes[4], &bytes[5]) != 6) {
        throw std::invalid_argument("Invalid MAC: " + mac);
    }

    for (std::size_t i = 0; i < 6; ++i) {
        result[i] = static_cast<std::uint8_t>(bytes[i]);
    }
    return result;
}

static std::uint32_t parseIp(const std::string& ip) {
    struct in_addr addr{};
    if (::inet_pton(AF_INET, ip.c_str(), &addr) != 1) {
        throw std::invalid_argument("Invalid IP: " + ip);
    }
    return addr.s_addr;  // уже в сетевом порядке
}


static std::uint16_t computeIpChecksum(const iphdr& ip) {
    const auto* data = reinterpret_cast<const std::uint16_t*>(&ip);
    std::uint32_t sum = 0;

    for (std::size_t i = 0; i < sizeof(ip) / 2; ++i) {
        sum += ntohs(data[i]);
    }
    while (sum >> 16) {
        sum = (sum & 0xFFFF) + (sum >> 16);
    }
    return htons(static_cast<std::uint16_t>(~sum));
}

void MakeRawUdpPacket(
    const std::string& srcMac,
    const std::string& dstMac,
    const std::string& srcIp,
    const std::string& dstIp,
    std::uint16_t srcPort,
    std::uint16_t dstPort,
    std::vector<std::byte>& buffer
) {
    const auto srcMacBytes = parseMac(srcMac);
    const auto dstMacBytes = parseMac(dstMac);
    const auto srcIpAddr   = parseIp(srcIp);
    const auto dstIpAddr   = parseIp(dstIp);

    constexpr std::size_t ETH_SIZE = sizeof(ethhdr);
    constexpr std::size_t IP_SIZE  = sizeof(iphdr);
    constexpr std::size_t UDP_SIZE = sizeof(udphdr);
    constexpr std::size_t HEADER_SIZE = ETH_SIZE + IP_SIZE + UDP_SIZE;

    buffer.resize(HEADER_SIZE);
    std::memset(buffer.data(), 0, HEADER_SIZE);

    auto* ptr = buffer.data();

    // ============================================================
    //  ETHERNET
    // ============================================================
    auto* eth = reinterpret_cast<ethhdr*>(ptr);
    std::memcpy(eth->h_dest, dstMacBytes.data(), 6);
    std::memcpy(eth->h_source, srcMacBytes.data(), 6);
    eth->h_proto = htons(0x0800);  // IPv4

    // ============================================================
    //  IPv4
    // ============================================================
    auto* ip = reinterpret_cast<iphdr*>(ptr + ETH_SIZE);
    ip->version = 0x45;  // IPv4, IHL = 5 (20 байт)
    ip->tos = 0;
    ip->tot_len = htons(static_cast<std::uint16_t>(IP_SIZE + UDP_SIZE));
    ip->id = 0;
    ip->frag_off  = 0;
    ip->ttl            = 64;
    ip->protocol       = 17;    // UDP
    ip->check = 0;     // сначала 0, потом посчитаем
    ip->saddr          = srcIpAddr;
    ip->daddr          = dstIpAddr;

    // Контрольная сумма IP
    ip->check = computeIpChecksum(*ip);

    // ============================================================
    //  UDP
    // ============================================================
    auto* udp = reinterpret_cast<udphdr*>(ptr + ETH_SIZE + IP_SIZE);
    udp->source  = htons(srcPort);
    udp->dest  = htons(dstPort);
    udp->len   = htons(static_cast<std::uint16_t>(UDP_SIZE));
    udp->check = 0;  // 0 = checksum отключена (разрешено для IPv4)
}

int main() {
    std::cout << "=== VNic Test (UDP) ===\n\n";

    vnic::VNic nic;

    //  КОНФИГУРАЦИЯ NIC
    vnic::VNic::Config config;
    config.rxQueueCount       = 4;
    config.txQueueCount       = 4;
    config.mtu                = 1500;
    config.promiscuousEnabled = true;

    // --- Link (физическое соединение) ---
    config.link.duplex          = vnic::Link::Config::Duplex::Full;
    config.link.autoNegotiation = true;
    config.link.speedMbps       = 10000;  // 10 Гбит/с

    // --- RSS ---
    config.rss.enabled  = true;
    config.rss.hf       = vnic::rss::HashFunc::TOEPLITZ;
    config.rss.protocol = vnic::rss::Protocol::ALL;
    config.rss.key      = std::vector<std::uint8_t>(40, 0x6D);

    if (!nic.configure(config)) {
        std::cerr << "Failed to configure NIC\n";
        return 1;
    }

    // --- RX-очереди ---
    for (std::uint16_t q = 0; q < config.rxQueueCount; ++q) {
        vnic::RxQueue::Config rxConfig;
        rxConfig.queueId  = q;
        rxConfig.size     = 512;  // 512 дескрипторов
        rxConfig.socketId = 0;
        if (!nic.configure(q, rxConfig)) {
            std::cerr << "Failed to configure RX queue " << q << "\n";
            return 1;
        }
    }

    // --- TX-очереди ---
    for (std::uint16_t q = 0; q < config.txQueueCount; ++q) {
        vnic::TxQueue::Config txConfig;
        txConfig.queueId  = q;
        txConfig.size     = 0;
        txConfig.socketId = 0;
        if (!nic.configure(q, txConfig)) {
            std::cerr << "Failed to configure TX queue " << q << "\n";
            return 1;
        }
    }

    if (!nic.getLink().up()) {
        std::cerr << "Failed to up link NIC\n";
        return 1;
    }

    std::cout << "NIC configured:\n";
    std::cout << "  RX queues: " << config.rxQueueCount << "\n";
    std::cout << "  TX queues: " << config.txQueueCount << "\n";
    std::cout << "  MTU: "       << config.mtu << "\n";
    std::cout << "  RSS: "       << (config.rss.enabled ? "enabled" : "disabled") << "\n";
    std::cout << "  RETA size: " << nic.getReta().size() << "\n\n";

    //  ЗАПУСК NIC
    std::thread rxThread{[&] {
        nic.start();  // блокирующий цикл RX-патруля; завершится по nic.stop()
    }};

    std::this_thread::sleep_for(std::chrono::milliseconds{60});

    //  ОТПРАВКА UDP-ПАКЕТОВ
    std::cout << "Sending UDP packets...\n";

    constexpr auto NUM_PACKETS{64};
    std::vector<std::byte> buffer;
    for (std::size_t i = 0; i < NUM_PACKETS; ++i) {
        MakeRawUdpPacket(
            "bb:bb:bb:bb:bb:bb", // srcMac
            "aa:aa:aa:aa:aa:aa", // dstMac
            "10.0.0.1",  // srcIp
            "10.0.0.2", // dstIp
            1234,   // srcPort
            4321,   // dstPort
            buffer
        );
        nic.getLink().write(buffer);
    }

    // ============================================================
    //  4. ОЖИДАНИЕ ОБРАБОТКИ
    // ============================================================
    std::this_thread::sleep_for(std::chrono::milliseconds{500});

    // ============================================================
    //  5. ПРИЛОЖЕНИЕ ЗАБИРАЕТ ПАКЕТЫ ИЗ RX-ОЧЕРЕДЕЙ
    // ============================================================
    std::vector<vnic::PacketDescriptor> rxDescs;
    std::uint64_t received = 0;
    // for (int round = 0; round < 16; ++round) {
    //     rxDescs.clear();
    //     const auto n = nic.rx(rxDescs);
    //     if (n == 0) break;
    //     received += n;
    //     nic.freeRx(rxDescs);
    // }

    std::cout << "App received " << received << " packets from RX queues\n";
    std::cout << "Buffer capacity: " << config.rxQueueCount * 512
              << " descriptors (остальные дропнуты по переполнению)\n\n";

    // ============================================================
    //  6. СТАТИСТИКА
    // ============================================================
    auto stats = nic.getStats();

    std::cout << "=== Statistics ===\n";
    std::cout << "  RX processed: " << stats.loadProcessedPackets()
              << " packets, " << stats.loadProcessedBytes() << " bytes\n";
    std::cout << "  RX dropped:   " << stats.loadDroppedPackets()
              << " packets, " << stats.loadDroppedBytes() << " bytes\n";
    std::cout << "  Overflows:    " << stats.loadOverflows() << "\n\n";

    // ============================================================
    //  7. СТАТИСТИКА ПО ОЧЕРЕДЯМ
    // ============================================================
    std::cout << "=== Per-Queue Statistics ===\n";
    for (std::uint16_t q = 0; q < config.rxQueueCount; ++q) {
        const auto qStats = nic.getRxQueue(q).getStats();
        std::cout << "  RX Queue " << q << ": "
                  << qStats.loadProcessedPackets() << " packets, "
                  << qStats.loadProcessedBytes() << " bytes, "
                  << qStats.loadOverflows() << " overflows\n";
    }
    std::cout << "\n";

    // ============================================================
    //  8. ПРОВЕРКА RETA
    // ============================================================
    std::cout << "=== RETA Distribution ===\n";
    const auto& reta = nic.getReta();
    std::array<std::size_t, 4> retaCounts{};
    for (auto queueId : reta) {
        if (queueId < 4) retaCounts[queueId]++;
    }
    for (std::size_t q = 0; q < 4; ++q) {
        std::cout << "  Queue " << q << ": "
                  << retaCounts[q] << " / " << reta.size()
                  << " (" << std::fixed << std::setprecision(1)
                  << (retaCounts[q] * 100.0 / reta.size()) << "%)\n";
    }
    std::cout << "\n";

    // ============================================================
    //  9. ОСТАНОВКА
    // ============================================================
    std::cout << "Stopping NIC...\n";
    nic.stop();

    if (rxThread.joinable()) {
        rxThread.join();
    }

    std::cout << "Done.\n";
    return 0;
}
