#include "include/queue/rx_queue.h"
#include  "include/constanst.h"

#include <array>

#include <cstring>

namespace vnic {

std::uint64_t RxQueue::put(IncomingPacket&& incomePacket) {
    const auto bytes{incomePacket.length};
    const auto segments{static_cast<std::uint64_t>(bytes / Packet::SEGMENT_SIZE)};

    std::array<PacketDescriptor, MAX_SEGMENTS> descs;
    const auto n{freeDescriptors_.popSome(descs)};
    if (n < segments) {
        // drop packet
    }

    for (decltype(n) i{0}; i < n; ++i) {
        PacketDescriptor& desc{descs[i]};
        desc.setMetadata(incomePacket);
        // recopy data
    }

    // Packet - это цепочка сегментов (next). Это ЧАСТИ ОДНОГО пакета,
    // а не отдельные пакеты. Их нельзя передать по частям — иначе
    // приложение получит только часть данных.
    //
    // Поэтому: сначала считаем, сколько нужно дескрипторов,
    // проверяем, что места хватит на ВСЕ, и только потом перемещаем.

    // Подсчёт сегментов
    std::size_t segments{0};
    std::size_t bytes{0};

    packet.forEachSegment([&](const Packet& p) {
        ++segments;
        bytes += p.length;
        return true;
    });

    // Если места меньше, чем сегментов - дропаем весь сетевой пакет
    if (free_.size() < segments) [[unlikely]] {
        stats_.fetchOverflows();
        stats_.fetchDroppedPackets();
        stats_.fetchDroppedBytes(bytes);
        return 0;
    }

    // Перемещение сетевого пакет в очередь
    packet.forEachSegment([&](Packet& p) {
        auto descr{free_.pop()};
        descr.setMetadata(p);
        std::memcpy(descr.data, p.data, p.length);
        used_.push(std::move(descr));
        return true;
    });

    stats_.fetchProcessedPackets();
    stats_.fetchProcessedBytes(bytes);
    notifyRxListener();
    return bytes;
}

void RxQueue::free(std::vector<PacketDescriptor>& descs) {
    free_.pushAll(descs);
    descs.clear();
}

std::size_t RxQueue::take(std::vector<PacketDescriptor>& descs) {
    const auto prevSize{descs.size()};
    std::array<PacketDescriptor, 64> storage;
    std::size_t n;
    do {
        n = used_.popSome(std::span{storage});
        assert(n <= storage.size());
        descs.insert(descs.cend(), storage.begin(), storage.begin() + n);
    } while (n == storage.size());
    return descs.size() - prevSize;
}

} // namespace vnic
