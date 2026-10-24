#include "queue/tx_queue.h"
#include "constants.h"
#include "packet/packet_buffer.h"

#include <vector>
#include <new>

#include <cstring>
#include <cassert>

namespace vnic {

bool TxQueue::Config::isValid() const noexcept { return true; }

std::expected<void, ErrorCode> TxQueue::put(PacketDescriptor&& desc) {
    if (desc.buffer == nullptr) [[unlikely]] {
        stats_.fetchDroppedPackets();
        stats_.fetchDroppedBytes(desc.totalLength);
        return std::unexpected(ErrorCode::TxDescriptorNull);
    }

    std::size_t segments{0};
    std::size_t total{0};
    PacketBuffer* it{desc.buffer};
    do {
        if (segments >= kMaxSegments) [[unlikely]] {
            stats_.fetchDroppedPackets();
            stats_.fetchDroppedBytes(desc.totalLength);
            return std::unexpected(ErrorCode::TxSegmentLimitExceeded);
        }
        ++segments;
        total += it->length;
        it = it->next;
    } while (it);

    if (total != desc.totalLength) [[unlikely]] {
        stats_.fetchDroppedPackets();
        stats_.fetchDroppedBytes(desc.totalLength);
        return std::unexpected(ErrorCode::TxLengthMismatch);
    }

    std::vector<std::byte> packet;
    try {
        packet.resize(total);
    } catch (const std::bad_alloc&) {
        stats_.fetchDroppedPackets();
        stats_.fetchDroppedBytes(desc.totalLength);
        return std::unexpected(ErrorCode::TxAllocationFailed);
    }

    std::size_t offset{0};
    it = desc.buffer;
    do {
        const auto& segment{it->getPacket()};
        if (!segment.empty()) {
            std::memcpy(packet.data() + offset, segment.data(), segment.size());
            offset += segment.size();
        }
        it = it->next;
    } while (it);

    if (link_ == nullptr) [[unlikely]] {
        stats_.fetchDroppedPackets();
        stats_.fetchDroppedBytes(desc.totalLength);
        return std::unexpected(ErrorCode::TxLinkUnavailable);
    }
    if (auto pushed = link_->putTx(packet); !pushed) [[unlikely]] {
        stats_.fetchDroppedPackets();
        stats_.fetchDroppedBytes(desc.totalLength);
        return std::unexpected(pushed.error() != ErrorCode::None ? pushed.error() : ErrorCode::TxLinkRejected);
    }

    stats_.fetchProcessedPackets();
    stats_.fetchProcessedBytes(desc.totalLength);
    return {};
}

std::expected<void, ErrorCode> TxQueue::configureImpl(const Config& config, Link& link) {
    if (!config.isValid()) {
        return std::unexpected(ErrorCode::QueueInvalidConfig);
    }
    config_ = config;
    link_   = &link;
    return {};
}

std::expected<void, ErrorCode> TxQueue::startImpl() { return {}; }

std::expected<void, ErrorCode> TxQueue::stopImpl() { return {}; }

const TxQueue::Config& TxQueue::getConfig() const { return config_; }

const stats::Stats& TxQueue::getStats() const { return stats_; }

const Link& TxQueue::getLink() const noexcept {
    assert(link_);
    return *link_;
}

} // namespace vnic
