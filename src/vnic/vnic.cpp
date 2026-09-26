#include "vnic/vnic.h"
#include "parser/parser.h"

#include <cassert>

namespace {

} // namespace

namespace vnic {

void VNic::distribute(PacketDescriptor&& packet, std::span<const std::byte> payload) {
    assert(!rxQueues_.empty());
    const auto& rssConfig{config_.rss};

    if (rssConfig.enabled) {
        assert(!reta_.empty() && "RETA is not initialized!");
        assert((reta_.size() & (reta_.size() - 1)) == 0 && "RETA size must be a power of 2!");
        packet.hash = rss::soft::calc_hash(Get5Tuple(packet), rssConfig.key, rssConfig.hf, rssConfig.protocol);
        packet.queueId = reta_[packet.hash & (reta_.size() - 1)];
    } else {
        assert(rxQueues_.size() == 1);
        packet.queueId = 0;
    }

    const auto bytes{payload.size()};
    assert(packet.queueId < rxQueues_.size());
    if (rxQueues_[packet.queueId].put(std::move(packet), payload)) {
        stats_.fetchProcessedPackets();
        stats_.fetchProcessedBytes(bytes);
    } else {
        stats_.fetchDroppedPackets();
        stats_.fetchDroppedBytes(bytes);
    }
}

std::int64_t VNic::rx(const std::uint16_t queueId, std::vector<PacketDescriptor>& descs) {
    if (queueId >= rxQueues_.size()) {
        return -1;
    }
    return rxQueues_[queueId].take(descs);
}

bool VNic::freeRx(const std::uint16_t queueId, std::vector<PacketDescriptor>&& descs) {
    // TODO
    return true;
}

bool VNic::validate(const PacketDescriptor& packet) const {
    return true; //TODO
}

bool VNic::l2Filter(const PacketDescriptor& packet) const {
    return true; //TODO
}

bool VNic::softOffloads(PacketDescriptor& packet) {
    return true; // TODO
}

bool VNic::initReta(Config::Rss::Reta& reta) {
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

        reta = config_.rss.reta;
        return true;
    }

    // Round-Robin
    constexpr auto RETA_SIZE{512};
    reta.resize(RETA_SIZE);
    for (std::uint16_t i{0}; i < RETA_SIZE; ++i) {
        reta[i] = i % config_.rxQueueCount;
    }
    return true;
}

bool VNic::configureImpl(const Config& config) {
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
    rxQueues_.resize(config_.rxQueueCount);

    txQueues_.clear();
    txQueues_.resize(config_.txQueueCount);
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

    Config::Rss::Reta reta;
    if (!initReta(reta)) [[unlikely]] {
        return false;
    }
    return updateReta(std::move(reta));
}

bool VNic::configureLink() {
    return link_.configure(config_.link);
}

bool VNic::configureImpl(const std::uint16_t queueId, const RxQueue::Config& config) {
    if (queueId >= rxQueues_.size()) [[unlikely]] {
        return false;
    }
    return rxQueues_[queueId].configure(config);
}

bool VNic::configureImpl(const std::uint16_t queueId, const TxQueue::Config& config) {
    if (queueId >= txQueues_.size()) [[unlikely]] {
        return false;
    }
    return txQueues_[queueId].configure(config);
}

bool VNic::startImpl() {
    if (!startQueues()) [[unlikely]] {
        return false;
    }

    thread_ = std::jthread{[this](std::stop_token st) { poll(st); }};
    return true;
}

void VNic::poll(std::stop_token stopToken) {
    std::vector<std::byte> buffer;
    buffer.reserve(PACKET_BUFFER_SIZE);

    while (!stopToken.stop_requested()) {
        const auto n{link_.receive(buffer, std::chrono::milliseconds{100})};
        if (n == 0) { // by timeout
            continue;
        }

        link_.waitForBandwidth(n);

        PacketDescriptor packet;
        if (!Parse(packet, std::span{buffer.data(), n})) [[unlikely]] {
            //
            continue;
        }

        if (!validate(packet)) [[unlikely]] {
            //
            continue;
        }

        if (!l2Filter(packet)) [[unlikely]] {
            //
            continue;
        }

        if (!softOffloads(packet)) [[unlikely]] {
            //
            continue;
        }

        distribute(std::move(packet), buffer);
    }
}

bool VNic::stopImpl() {
    thread_.request_stop();
    if (thread_.joinable()) {
        thread_.join();
    }
    return stopQueues();
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

bool VNic::updateReta(Config::Rss::Reta&& reta) {
    reta_ = std::move(reta); //! TODO: пока не потока-безопасно
    return true;
}

bool VNic::updateReta(const Config::Rss::Reta& reta) {
    return updateReta(Config::Rss::Reta{reta});
}

void VNic::resetStats() {
    stats_.reset();
}

Link& VNic::getLink() {
    return link_;
}

const Link& VNic::getLink() const {
    return link_;
}

const stats::Stats& VNic::getStats() const {
    return stats_;
}

enum VNic::ErrorCode VNic::getErrorCode() const {
    return eCode_;
}

const VNic::Config::Rss::Reta& VNic::getReta() const {
    return reta_;
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
