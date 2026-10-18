#include "queue/tx_queue.h"
#include "packet/packet_buffer.h"
#include "constanst.h"

#include <vector>

#include <cstdint>
#include <cstring>

namespace vnic {

bool TxQueue::put(PacketDescriptor&& desc) {
    if (desc.buffer == nullptr) [[unlikely]] {
        stats_.fetchDroppedPackets();
        stats_.fetchDroppedBytes(desc.totalLength);
        return false;
    }

    std::size_t segments{0};
    std::size_t total{0};
    PacketBuffer* it{desc.buffer};
    do {
        if (segments >= MAX_SEGMENTS) [[unlikely]] {
            stats_.fetchDroppedPackets();
            stats_.fetchDroppedBytes(desc.totalLength);
            return false;
        }
        ++segments;
        total += it->length;
        it = it->next;
    } while (it);

    assert(total == desc.totalLength);

    std::vector<std::byte> packet;
    packet.resize(total);

    std::size_t offset{0};
    it = desc.buffer;
    do {
        const auto& segment{it->getPacket()};
        std::memcpy(packet.data() + offset, segment.data(), segment.size());
        offset += segment.size();
        it = it->next;
    } while (it);

    if (link_ == nullptr) [[unlikely]] {
        stats_.fetchDroppedPackets();
        stats_.fetchDroppedBytes(desc.totalLength);
        return false;
    }
    if (!link_->putTx(packet)) [[unlikely]] {
        stats_.fetchDroppedPackets();
        stats_.fetchDroppedBytes(desc.totalLength);
        return false;
    }

    stats_.fetchProcessedPackets();
    stats_.fetchProcessedBytes(desc.totalLength);
    return true;
}

bool TxQueue::configureImpl(const Config& config, Link& link) {
    config_ = config;
    link_   = &link;
    return true;
}

bool TxQueue::startImpl() { return true; }

bool TxQueue::stopImpl() { return true; }

const TxQueue::Config& TxQueue::getConfig() const { return config_; }

const stats::Stats& TxQueue::getStats() const { return stats_; }

const Link* TxQueue::getLink() const noexcept { return link_; }

} // namespace vnic
