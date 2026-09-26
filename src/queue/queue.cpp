#include "queue/queue.h"

#include <cassert>

namespace vnic {

bool Queue::configureImpl(const Config& config) {
    config_ = config;

    const memory::Pool::Config memConfig{
        .blockSize = PACKET_BUFFER_SIZE,
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
        auto buffer{memory_.allocate(PACKET_BUFFER_SIZE)};
        if (!buffer) [[unlikely]] {
            return false;
        }

        PacketDescriptor desc;
        if (!free_.push(std::move(desc))) [[unlikely]] {
            // TODO: очислить аллоцированное
            return false;
        }
    }

    return true;
}

bool Queue::stopImpl() {
    while (!free_.isEmpty()) {
        (void)free_.pop();
    }

    while (!used_.isEmpty()) {
        (void)used_.pop();
    }

    return true;
}

const Queue::Config& Queue::getConfig() const { return config_; }
const stats::Stats& Queue::getStats() const { return stats_; }

} // namespace vnic
