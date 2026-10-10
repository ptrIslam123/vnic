#include "queue/tx_queue.h"
#include "packet/packet_buffer.h"
#include "constanst.h"

#include <array>

#include <cassert>

namespace vnic {

bool TxQueue::put(std::span<const PacketDescriptor> descs) {
    for (auto desc : descs) {
        if (!put(std::move(desc))) [[unlikely]] {
            return false;
        }
    }
    return true;
}

bool TxQueue::put(PacketDescriptor&& desc) {
    if (desc.buffer) [[unlikely]] {
        assert(desc.buffer);
        return false;
    }

    std::array<std::span<std::byte>, MAX_SEGMENTS> segments;
    PacketBuffer* it{desc.buffer};
    std::size_t i{0};
    do {
        segments[i++] = it->getPacket();
        if (i > segments.size()) {
            assert(i <= segments.size());
            return false;
        }
        it = it->next;
    } while (it);

    if (link_->putTx(std::span<std::span<std::byte>>{ segments.data(), i + 1 })) {
        stats_.fetchProcessedPackets();
        stats_.fetchProcessedBytes(desc.totalLength);
    } else {
        stats_.fetchDroppedPackets();
        stats_.fetchDroppedBytes(desc.totalLength);
    }
}

bool TxQueue::configureImpl(const Config& config) {
    config_ = config;
    return true;
}

bool TxQueue::startImpl() {
    return true;
}

bool TxQueue::stopImpl() {
    return true;
}

const TxQueue::Config& TxQueue::getConfig() const {
    return config_;
}

const stats::Stats& TxQueue::getStats() const {
    return stats_;
}

} // namespace vnic
