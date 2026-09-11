#include "include/vnic/vnic.h"

#include <cassert>

namespace {

} // namespace

namespace vnic {

void VNic::rx(Packet&& packet) {
    link_.enqueue(std::move(packet));
}

bool VNic::validate(const Packet& packet) const {
    return true; //TODO
}

bool VNic::l2Filter(const Packet& packet) const {
    return true; //TODO
}

bool VNic::softOffloads(Packet& packet) {
    return true; // TODO
}

void VNic::distribute(Packet&& packet) {
    assert(!rxQueues_.empty());
    const auto& rssConfig{config_.rss};
    if (!rssConfig.enabled) {
        rxQueues_[0].push(std::move(packet));
    } else {
        assert(reta_.size() > 0 && "RETA is not initialized!");
        assert((reta_.size() & (reta_.size() - 1)) == 0 && "RETA size must be a power of 2!");
        const auto hash{rss::soft::calc_hash(packet, rssConfig.key, rssConfig.hf, rssConfig.protocol)};
        const auto queueId{reta_[hash & (reta_.size() - 1)]};
        rxQueues_[queueId].push(std::move(packet));
    }
}

void VNic::notify_rx() {
    //TODO
}

bool VNic::initReta() {
    const auto& rssConfig{config_.rss};
    assert(rssConfig.enabled);

    if (!config_.rss.reta.empty()) {
        const auto size = config_.rss.reta.size();
        if ((size & (size - 1)) != 0) {
            return false;  // RETA size must be a power of 2!
        }

        for (auto queueId : config_.rss.reta) {
            if (queueId >= config_.rxQueueCount) {
                return false;  // invalid queue idx
            }
        }

        reta_ = config_.rss.reta;
        return true;
    }

    // Round-Robin
    constexpr auto RETA_SIZE{512};
    reta_.resize(RETA_SIZE);
    for (std::uint16_t i{0}; i < RETA_SIZE; ++i) {
        reta_[i] = i % config_.rxQueueCount;
    }
    return true;
}

bool VNic::configure(const Config& config) {
    config_ = config;

    if (!configureLink()) [[unlikely]] {
        return false;
    }

    if (!configureQueues()) [[unlikely]] {
        return false;
    }

    if (!configureRss()) [[unlikely]] {
        return false;
    }

    return true;
}

bool VNic::configureQueues() {
    rxQueues_.clear();
    rxQueues_.reserve(config_.rxQueueCount);
    for (decltype(config_.rxQueueCount) i{0}; i < config_.rxQueueCount; ++i) {
        rxQueues_.emplace({});
    }

    txQueues_.clear();
    txQueues_.reserve(config_.txQueueCount);
    for (decltype(config_.txQueueCount) i{0}; i < config_.txQueueCount; ++i) {
        txQueues_.emplace({});
    }
    return true;
}

bool VNic::configureRss() {
    static constexpr auto MIN_KEY_LEN{40};
    const auto rssConfig{config_.rss};
    if (!rssConfig.enabled) {
        return true;
    }

    if (rssConfig.key.size() < MIN_KEY_LEN) {
        return false;
    }
    return initReta();
}

bool VNic::configureLink() {
    return false;
}

bool VNic::configure(const std::uint16_t queueId, const RxQueue::Config& config) {
    if (queueId >= rxQueues_.size()) [[unlikely]] {
        return false;
    }

    return rxQueues_[queueId].configure(config);
}

bool VNic::configure(const std::uint16_t queueId, const TxQueue::Config& config) {
    if (queueId >= txQueues_.size()) [[unlikely]] {
        return false;
    }

    return txQueues_[queueId].configure(config);
}

bool VNic::start() {
    if (startQueues()) [[unlikely]] {
        return false;
    }

    while (getState() == State::Started) {
        auto&& packet{link_.dequeue()}; // blocking call
        if (!validate(packet)) [[unlikely]] {
            continue;
        }

        if (!l2Filter(packet)) [[unlikely]] {
            continue;
        }

        if (!softOffloads(packet)) [[unlikely]] {
            continue;
        }

        distribute(std::move(packet));
        notify_rx();
    }
    return true;
}

bool VNic::startQueues() {
    for (auto& queue : rxQueues_) {
        if (!queue.start()) [[unlikely]] {
            return false;
        }
    }
    for (auto& queue : txQueues_) {
        if (!queue.start()) [[unlikely]] {
            return false;
        }
    }
    return true;
}

bool VNic::stopQueues() {
    for (auto& queue : rxQueues_) {
        if (!queue.stop()) [[unlikely]] {
            return false;
        }
    }
    for (auto& queue : txQueues_) {
        if (!queue.stop()) [[unlikely]] {
            return false;
        }
    }
    return true;
}

bool VNic::stop() {
    return true;
}

bool VNic::updateReta(Config::RssConfig::Reta&& reta) {
    return false; //TODO
}

bool VNic::updateReta(const Config::RssConfig::Reta& reta) {
    return updateReta(Config::RssConfig::Reta{reta});
}

void VNic::resetStats() {
    //TODO
}

const stats::Stats& VNic::getStats() const {
    return stats_;
}

enum VNic::ErrorCode VNic::getErrorCode() const {
    return eCode_;
}

const VNic::Config& VNic::getConfig() const {
    return config_;
}

const RxQueue& VNic::getRxQueue(std::uint16_t queueId) const {
    assert(queueId < rxQueues_.size());
    return rxQueues_[queueId];
}

const TxQueue& VNic::getTxQueue(std::uint16_t queueId) const {
    assert(queueId < txQueues_.size());
    return txQueues_[queueId];
}

} // namespace vnic
