#pragma once

#include "queue/queue.h"
#include "packet/packet_buffer.h"
#include "constanst.h"

#include <algorithm>
#include <span>

#include <cstring>
#include <cassert>

namespace vnic {

class RxQueue final : public Queue {
public:
    struct Config : Queue::Config {};

    template<typename P>
    bool put(P&& packet);
    std::uint64_t take(std::vector<PacketDescriptor>& descs);
    void free(std::vector<PacketDescriptor>&& descs);
};

template<typename P>
bool RxQueue::put(P&& packet) {
    std::array<PacketDescriptor, MAX_SEGMENTS> descs;
    const auto bytes{packet.length()};
    const auto segments{static_cast<std::uint64_t>((bytes + PACKET_BUFFER_SIZE - 1) / PACKET_BUFFER_SIZE)};
    assert(segments <= MAX_SEGMENTS);

    const auto n{free_.popSome(descs)};
    if (n < segments) {
        state_.fetchOverflows();
        stats_.fetchDroppedPackets();
        stats_.fetchDroppedBytes(bytes);
        return false;
    }

    for (decltype(n) i{0}; i < n; ++i) {
        PacketDescriptor& desc{descs[i]};
        desc.metadata = packet.getMetadata();
        const auto offset = i * PACKET_BUFFER_SIZE;
        const auto size{std::min(PACKET_BUFFER_SIZE, bytes - offset)};
        std::memcpy(desc.buffer.memory, packet.data() + offset, size);
    }

    used_.pushAll(descs);
    stats_.fetchProcessedPackets();
    stats_.fetchProcessedBytes(bytes);
    return true;
}

std::uint64_t RxQueue::take(std::vector<PacketDescriptor>& descs) {
    std::array<PacketDescriptor, MAX_SEGMENTS> storage;
    const auto n{used_.popSome(storage)};
    descs.insert(descs.cend(), storage.cbegin(), storage.cbegin() + n);
}

void RxQueue::free(std::vector<PacketDescriptor>&& descs) {
    free_.pushAll(desc);
}

} // namespace vnic
