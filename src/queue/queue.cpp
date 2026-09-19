#include "queue/queue.h"

#include <cassert>

namespace vnic {

bool Queue::configureImpl(const Config& config) {
    config_ = config;

    const memory::Pool::Config memConfig{
        .blockSize = Packet::SEGMENT_SIZE,
        .capacity  = config_.size,
        .socketId  = config_.socketId
    };
    if (!memory_.configure(memConfig)) [[unlikely]] {
        return false;
    }

    return true;
}

bool Queue::startImpl() {
    free_.reserve(config_.size);
    used_.reserve(config_.size);

    for (decltype(config_.size) i{0}; i < config_.size; ++i) {
        auto buffer{memory_.allocate(Packet::SEGMENT_SIZE)};
        if (!buffer) [[unlikely]] {
            return false;
        }

        PacketDescriptor desc;
        desc.data = buffer;
        free_.push(std::move(desc));
    }

    return true;
}

bool Queue::stopImpl() {
    while (!free_.isEmpty()) {
        auto&& desc{free_.pop()};
        memory_.deallocate(desc.data);
    }

    while (!used_.isEmpty()) {
        auto&& desc{used_.pop()};
        memory_.deallocate(desc.data);
    }

    return true;
}

void Queue::notifyRxListener() {
    // TODO
}

const Queue::Config& Queue::getConfig() const { return config_; }
const stats::Stats& Queue::getStats() const { return stats_; }

} // namespace vnic
