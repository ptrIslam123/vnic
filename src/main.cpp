#include "vnic/vnic.h"
#include "packet/packet.h"

#include <thread>
#include <array>

#include <cstring>

#include "vnic/vnic.h"
#include "packet/packet.h"

#include <array>
#include <vector>
#include <thread>
#include <chrono>
#include <cstring>
#include <iostream>
#include <iomanip>
#include <atomic>

// ============================================================
//  ГЕНЕРАЦИЯ ТЕСТОВЫХ ПАКЕТОВ
// ============================================================

// Ethernet + IPv4 + TCP заголовок (упрощённый)
struct TcpPacket {
    // Ethernet
    std::uint8_t  dstMac[6];
    std::uint8_t  srcMac[6];
    std::uint16_t etherType;      // 0x0800 = IPv4

    // IPv4
    std::uint8_t  versionIhl;     // 0x45
    std::uint8_t  tos;
    std::uint16_t totalLength;
    std::uint16_t identification;
    std::uint16_t flagsFragment;
    std::uint8_t  ttl;
    std::uint8_t  protocol;       // 6 = TCP
    std::uint16_t headerChecksum;
    std::uint32_t srcIp;
    std::uint32_t dstIp;

    // TCP
    std::uint16_t srcPort;
    std::uint16_t dstPort;
    std::uint32_t seqNum;
    std::uint32_t ackNum;
    std::uint8_t  dataOffset;
    std::uint8_t  flags;
    std::uint16_t window;
    std::uint16_t checksum;
    std::uint16_t urgentPointer;

    // Payload
    std::uint8_t payload[16];
} __attribute__((packed));

static TcpPacket makePacket(std::uint32_t srcIp, std::uint32_t dstIp,
                            std::uint16_t srcPort, std::uint16_t dstPort,
                            std::uint8_t fillByte)
{
    TcpPacket pkt{};

    // Ethernet
    std::memset(pkt.dstMac, 0xAA, 6);
    std::memset(pkt.srcMac, 0xBB, 6);
    pkt.etherType = 0x0008;  // little-endian 0x0800

    // IPv4
    pkt.versionIhl = 0x45;
    pkt.totalLength = 0x2800;  // little-endian 40
    pkt.ttl = 64;
    pkt.protocol = 6;  // TCP
    pkt.srcIp = srcIp;
    pkt.dstIp = dstIp;

    // TCP
    pkt.srcPort = srcPort;
    pkt.dstPort = dstPort;
    pkt.dataOffset = 0x50;  // 5 * 4 = 20 байт
    pkt.flags = 0x02;       // SYN

    // Payload
    std::memset(pkt.payload, fillByte, sizeof(pkt.payload));

    return pkt;
}

// ============================================================
//  MAIN
// ============================================================

int main() {
    std::cout << "=== VNic Test ===\n\n";

    vnic::VNic nic;

    // ============================================================
    //  1. КОНФИГУРАЦИЯ NIC
    // ============================================================
    vnic::VNic::Config config;
    config.rxQueueCount = 4;
    config.txQueueCount = 4;
    config.mtu = 1500;
    config.promiscuousEnabled = true;

    // Link (физическое соединение) ---
    config.link.duplex = vnic::Link::Config::Duplex::Full;
    config.link.autoNegotiation = true;
    config.link.speedMbps = 10000;  // 10 Гбит/с

    // RSS
    config.rss.enabled = true;
    config.rss.hf = vnic::rss::HashFunc::TOEPLITZ;
    config.rss.protocol = vnic::rss::Protocol::ALL;
    config.rss.key = std::vector<std::uint8_t>(40, 0x6D);  // 40 байт ключа

    if (!nic.configure(config)) {
        std::cerr << "Failed to configure NIC\n";
        return 1;
    }

    // --- RX очередь 0 ---
    vnic::RxQueue::Config rxConfig;
    rxConfig.queueId = 0;
    rxConfig.size = 512;              // ← 512 дескрипторов
    rxConfig.socketId = 0;            // ← NUMA-узел 0
    // rxConfig.offloads.checksumIp = true;
    // rxConfig.offloads.checksumTcp = true;
    // rxConfig.offloads.vlanStrip = true;
    if (!nic.configureQueue(0, rxConfig)) {
        std::cerr << "Failed to configure RX queue 0\n";
        return 1;
    }

    if (!nic.upLink()) {
        std::cerr << "Failed to up link NIC\n";
        return 1;
    }

    std::cout << "NIC configured:\n";
    std::cout << "  RX queues: " << config.rxQueueCount << "\n";
    std::cout << "  TX queues: " << config.txQueueCount << "\n";
    std::cout << "  MTU: " << config.mtu << "\n";
    std::cout << "  RSS: " << (config.rss.enabled ? "enabled" : "disabled") << "\n";
    std::cout << "  RETA size: " << nic.getReta().size() << "\n\n";

    // ============================================================
    //  2. ЗАПУСК NIC
    // ============================================================
    std::atomic<bool> running{true};

    std::thread rxThread{[&] {
        nic.start();  // ← блокирующий цикл
    }};

    std::this_thread::sleep_for(std::chrono::milliseconds{100});

    // ============================================================
    //  3. ОТПРАВКА ПАКЕТОВ
    // ============================================================
    std::cout << "Sending packets...\n";

    constexpr std::size_t NUM_PACKETS = 10000;
    constexpr std::size_t NUM_FLOWS = 100;

    auto startTime = std::chrono::steady_clock::now();

    for (std::size_t i = 0; i < NUM_PACKETS; ++i) {
        // Разные потоки (разные IP/порты) → разный RSS-хэш
        std::uint32_t srcIp = 0x0A000001 + (i % NUM_FLOWS);  // 10.0.0.1 + flow
        std::uint32_t dstIp = 0x0A000002;
        std::uint16_t srcPort = 1024 + (i % NUM_FLOWS);
        std::uint16_t dstPort = 80;

        auto raw = makePacket(srcIp, dstIp, srcPort, dstPort,
                              static_cast<std::uint8_t>(i & 0xFF));

        // Создать пакет
        vnic::Packet packet;
        packet.length = sizeof(TcpPacket);
        packet.totalLength = sizeof(TcpPacket);
        packet.timestamp = std::chrono::steady_clock::now().time_since_epoch().count();
        packet.hash = srcIp ^ (srcPort << 16);
        packet.offloads = 0;
        packet.next = nullptr;
        std::memcpy(packet.data, &raw, sizeof(TcpPacket));

        // Отправить в NIC
        nic.rx(std::move(packet));
    }

    auto endTime = std::chrono::steady_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(
                       endTime - startTime).count();

    std::cout << "Sent " << NUM_PACKETS << " packets in "
              << elapsed << " us\n";
    std::cout << "Rate: " << (NUM_PACKETS * 1'000'000.0 / elapsed)
              << " packets/sec\n\n";

    // ============================================================
    //  4. ОЖИДАНИЕ ОБРАБОТКИ
    // ============================================================
    std::this_thread::sleep_for(std::chrono::milliseconds{500});

    // ============================================================
    //  5. СТАТИСТИКА
    // ============================================================
    auto stats = nic.getStats();

    std::cout << "=== Statistics ===\n";
    // std::cout << "  RX packets:  " << stats.rxPackets << "\n";
    // std::cout << "  RX bytes:    " << stats.rxBytes << "\n";
    // std::cout << "  RX dropped:  " << stats.rxDropped << "\n";
    // std::cout << "  RX errors:   " << stats.rxErrors << "\n";
    // std::cout << "  TX packets:  " << stats.txPackets << "\n";
    // std::cout << "  TX bytes:    " << stats.txBytes << "\n";
    // std::cout << "  Overflows:   " << stats.overflows << "\n\n";

    // ============================================================
    //  6. СТАТИСТИКА ПО ОЧЕРЕДЯМ
    // ============================================================
    std::cout << "=== Per-Queue Statistics ===\n";
    for (std::size_t q = 0; q < config.rxQueueCount; ++q) {
        const auto& rxQueue = nic.getRxQueue(q);
        auto qStats = rxQueue.getStats();
        // std::cout << "  RX Queue " << q << ": "
        //           << qStats.processedPackets << " packets, "
        //           << qStats.processedBytes << " bytes\n";
    }
    std::cout << "\n";

    // ============================================================
    //  7. ПРОВЕРКА RETA
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
                  << " (" << (retaCounts[q] * 100.0 / reta.size()) << "%)\n";
    }
    std::cout << "\n";

    // ============================================================
    //  8. ОСТАНОВКА
    // ============================================================
    std::this_thread::sleep_for(std::chrono::seconds{60});
    std::cout << "Stopping NIC...\n";
    nic.stop();

    if (rxThread.joinable()) {
        rxThread.join();
    }

    std::cout << "Done.\n";
    return 0;
}