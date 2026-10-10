#include "queue/rx_queue.h"
#include "packet/packet_buffer.h"
#include "utils/scoped_guard.h"
#include "constanst.h"

#include <array>

#include <cstdint>
#include <cassert>

namespace vnic {

bool RxQueue::put(PacketDescriptor&& desc, std::span<const std::byte> packet) {
    const auto bytes{packet.size()};
    assert(packet.size() == bytes && "payload size must match packet.length");
    if (bytes == 0 || free_.isEmpty()) [[unlikely]] {
        return false;
    }

    const auto segments{static_cast<std::uint64_t>((bytes + PACKET_BUFFER_SIZE - 1) / PACKET_BUFFER_SIZE)};
    assert(segments <= MAX_SEGMENTS);

    // Выделяем все сегменты, не связывая
    std::array<PacketBuffer*, MAX_SEGMENTS> buffers;
    std::uint64_t bufferIdx{0};
    utils::ScopedGuard buffersGuard{[&] {
        for (std::uint64_t j{0}; j < bufferIdx; ++j) memPool_.deallocate(buffers[j]);
    }};
    for (; bufferIdx < segments; ++bufferIdx) {
        buffers[bufferIdx] = static_cast<PacketBuffer*>(memPool_.allocate());
        if (!buffers[bufferIdx]) [[unlikely]] {
            return false;
        }
    }

    // Заполняем заголовки и связываем
    std::size_t offset{0};
    for (std::uint64_t segmentIdx{0}; segmentIdx < segments; ++segmentIdx) {
        auto buffer{buffers[segmentIdx]};
        const auto chunk{std::min<std::size_t>(PACKET_BUFFER_SIZE, bytes - offset)};
        buffer->length = static_cast<std::uint32_t>(chunk);
        buffer->next = (segmentIdx + 1 < segments) ? buffers[segmentIdx + 1] : nullptr;
        std::memcpy(reinterpret_cast<std::byte*>(buffer + 1), packet.data() + offset, chunk);
        offset += chunk;
    }
    desc.buffer = buffers[0];
    desc.totalLength = static_cast<std::uint32_t>(bytes);

    (void)free_.pop();
    if (!used_.push(std::move(desc))) [[unlikely]] {
        // TODO: очислить аллооцированные ресурсы
        return false;
    }

    stats_.fetchProcessedPackets();
    stats_.fetchProcessedBytes(bytes);
    buffersGuard.cancel();
    return true;
}

// Забирает из used_ - очереди все накопленные дескрипторы (не блокируется),
// возвращает количество изъятых дескрипторов.
std::uint64_t RxQueue::take(std::vector<PacketDescriptor>& descs) {
    std::array<PacketDescriptor, MAX_SEGMENTS> storage;
    const auto n{used_.tryPopSome(std::span{storage})};
    descs.insert(descs.cend(), storage.cbegin(), storage.cbegin() + n);
    return n;
}

// Возвращает дескрипторы обратно в пул свободных (буферы вновь могут
// использоваться для приёма следующих пакетов).
bool RxQueue::free(std::vector<PacketDescriptor>&& descs) {
    free_.pushAll(std::span<const PacketDescriptor>{descs});
    return true;
}
bool RxQueue::free(PacketDescriptor&& desc) {
    free_.push(desc);
    return true;
}

} // namespace vnic