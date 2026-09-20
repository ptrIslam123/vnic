#include "include/queue/rx_queue.h"
#include "include/constanst.h"

#include <array>

#include <cstdint>
#include <cassert>

namespace vnic {

// Принимает пакет (его данные уже скопированы в буферы memory pool этой
// очереди), возвращает true, если пакет встал в очередь used_. Если свободных
// дескрипторов не хватило — пакет дропается целиком, счётчики обновляются.
bool RxQueue::put(IncomingPacket&& packet) {
    const auto bytes{packet.length};
    const auto segments{static_cast<std::uint64_t>((bytes + PACKET_BUFFER_SIZE - 1) / PACKET_BUFFER_SIZE)};
    assert(segments <= MAX_SEGMENTS);
    if (segments == 0) [[unlikely]] {
        return false;
    }

    if (free_.size() < segments) [[unlikely]] {
        stats_.fetchOverflows();
        stats_.fetchDroppedPackets();
        stats_.fetchDroppedBytes(bytes);
        return false;
    }

    std::array<PacketDescriptor, MAX_SEGMENTS> descs;
    const auto n{free_.popSome(std::span{descs.data(), segments})};
    if (n < segments) {
        // Места не хватило на ВЕСЬ сетевой пакет — дропаем целиком и
        // возвращаем уже изъятые дескрипторы обратно в free_.
        free_.pushAll(std::span<const PacketDescriptor>{descs.data(), n});

        stats_.fetchOverflows();
        stats_.fetchDroppedPackets();
        stats_.fetchDroppedBytes(bytes);
        return false;
    }

    for (std::size_t i{0}; i < segments; ++i) {
        PacketDescriptor& desc{descs[i]};
        desc.metadata = packet.getMetadata();
        const auto offset{static_cast<std::uint32_t>(i * PACKET_BUFFER_SIZE)};
        const auto size{std::min<std::uint32_t>(PACKET_BUFFER_SIZE, bytes - offset)};
        std::memcpy(desc.buffer.memory, packet.data + offset, size);
        desc.buffer.length = size;
    }

    used_.pushAll(std::span{descs.data(), segments});
    stats_.fetchProcessedPackets();
    stats_.fetchProcessedBytes(bytes);
    notifyRxListener();
    return true;
}

// Забирает из used_-очереди все накопленные дескрипторы (не блокируется),
// возвращает количество изъятых дескрипторов.
std::uint64_t RxQueue::take(std::vector<PacketDescriptor>& descs) {
    std::array<PacketDescriptor, MAX_SEGMENTS> storage;
    const auto n{used_.tryPopSome(std::span{storage})};
    descs.insert(descs.cend(), storage.cbegin(), storage.cbegin() + n);
    return n;
}

// Возвращает дескрипторы обратно в пул свободных (буферы вновь могут
// использоваться для приёма следующих пакетов).
void RxQueue::free(std::vector<PacketDescriptor>&& descs) {
    free_.pushAll(std::span<const PacketDescriptor>{descs});
}

} // namespace vnic