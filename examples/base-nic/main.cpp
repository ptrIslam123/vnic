#include "vnic/vnic.h"

#include <array>
#include <vector>
#include <thread>
#include <chrono>
#include <cstring>
#include <iostream>
#include <iomanip>

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

constexpr std::size_t NUM_PACKETS = 10000;
constexpr std::size_t NUM_FLOWS = 100;

// Буферы под пакеты, уходящие на "линк": NIC копирует данные в свой
// memory pool асинхронно (патрульный поток), поэтому нельзя использовать
// стековые локальные переменные — они умирают при выходе из итерации.
static std::uint8_t rawBuffers[NUM_PACKETS * sizeof(TcpPacket)];

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

    // Все RX-очереди (без configure() очередь нельзя стартовать)
    for (std::uint16_t q = 0; q < config.rxQueueCount; ++q) {
        vnic::RxQueue::Config rxConfig;
        rxConfig.queueId = q;
        rxConfig.size = 512;              // 512 дескрипторов
        rxConfig.socketId = 0;            // NUMA-узел 0
        if (!nic.configure(q, rxConfig)) {
            std::cerr << "Failed to configure RX queue " << q << "\n";
            return 1;
        }
    }

    // Все TX-очереди (в v1 TX-дескрипторов не выделяем)
    for (std::uint16_t q = 0; q < config.txQueueCount; ++q) {
        vnic::TxQueue::Config txConfig;
        txConfig.queueId = q;
        txConfig.size = 0;
        txConfig.socketId = 0;
        if (!nic.configure(q, txConfig)) {
            std::cerr << "Failed to configure TX queue " << q << "\n";
            return 1;
        }
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

    std::thread rxThread{[&] {
        nic.start();  // блокирующий цикл RX-патруля; завершится по nic.stop()
    }};

    std::this_thread::sleep_for(std::chrono::milliseconds{60});

    // ============================================================
    //  3. ОТПРАВКА ПАКЕТОВ
    // ============================================================
    std::cout << "Sending packets...\n";

    auto startTime = std::chrono::steady_clock::now();

    for (std::size_t i = 0; i < NUM_PACKETS; ++i) {
        // Разные потоки (разные IP/порты) → разный RSS-хэш
        std::uint32_t srcIp = 0x0A000001 + (i % NUM_FLOWS);  // 10.0.0.1 + flow
        std::uint32_t dstIp = 0x0A000002;
        std::uint16_t srcPort = 1024 + (i % NUM_FLOWS);
        std::uint16_t dstPort = 80;

        auto raw = makePacket(srcIp, dstIp, srcPort, dstPort,
                              static_cast<std::uint8_t>(i & 0xFF));

        // Копируем в собственный слот (см. rawBuffers выше)
        auto* rawPtr = rawBuffers + i * sizeof(TcpPacket);
        std::memcpy(rawPtr, &raw, sizeof(TcpPacket));

        // Пакет на линии: NIC не владеет data_, а копирует его в свой пул
        vnic::IncomingPacket packet;
        packet.data     = rawPtr;
        packet.length   = sizeof(TcpPacket);
        packet.hash     = srcIp ^ (srcPort << 16);
        packet.queueId  = 0;
        packet.offloads = 0;

        // Отправить в NIC (RX-вход линка)
        nic.process(std::move(packet));
    }

    auto endTime = std::chrono::steady_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(
                       endTime - startTime).count();

    std::cout << "Sent " << NUM_PACKETS << " packets in "
              << elapsed << " us\n";
    std::cout << "Rate: " << (NUM_PACKETS * 1'000'000.0 / elapsed)
              << " packets/sec\n";

    // ============================================================
    //  4. ОЖИДАНИЕ ОБРАБОТКИ
    // ============================================================
    std::this_thread::sleep_for(std::chrono::milliseconds{500});

    // ============================================================
    //  5. ПРИЛОЖЕНИЕ ЗАБИРАЕТ ПАКЕТЫ ИЗ RX-ОЧЕРЕДЕЙ (v1 API)
    // ============================================================
    std::vector<vnic::PacketDescriptor> rxDescs;
    std::uint64_t received = 0;
    for (int round = 0; round < 16; ++round) {
        rxDescs.clear();
        const auto n = nic.rx(rxDescs);
        if (n == 0) {
            break;
        }
        received += n;
        nic.freeRx(rxDescs);
    }

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
    nic.stop();  // корректно выходит из блокирующего цикла RX-патруля

    if (rxThread.joinable()) {
        rxThread.join();
    }

    std::cout << "Done.\n";
    return 0;
}