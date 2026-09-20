#include "include/vnic/vnic.h"

#include <cassert>

namespace {

} // namespace

namespace vnic {

void VNic::process(IncomingPacket&& packet) {
    link_.put(std::move(packet));
}

std::uint64_t VNic::rx(std::vector<PacketDescriptor>& descs) {
    std::uint64_t received{0};
    for (auto& queue : rxQueues_) {
        received += queue.take(descs);
    }
    return received;
}

void VNic::freeRx(std::vector<PacketDescriptor>& descs) {
    // Каждый дескриптор возвращаем в пул той очереди, которая его накопила
    // (buffer принадлежит его memory pool). queueId записываем при приёме.
    for (auto& desc : descs) {
        const auto queueId = desc.metadata.queueId;
        if (queueId >= rxQueues_.size()) [[unlikely]] {
            continue;
        }

        std::vector<PacketDescriptor> chunk;
        chunk.push_back(std::move(desc));
        rxQueues_[queueId].free(std::move(chunk));
    }
    descs.clear();
}

std::uint64_t VNic::tx(std::vector<PacketDescriptor>& descs) {
    // v1: loopback — дескрипторы TX уходят обратно на линк и проходят
    // полный RX-путь. Много-сегментные пакеты (next) — вне v1.
    std::uint64_t sent{0};
    for (auto& desc : descs) {
        if (desc.buffer.length == 0 || desc.buffer.memory == nullptr) [[unlikely]] {
            continue;
        }

        IncomingPacket packet;
        packet.data     = desc.buffer.memory;
        packet.length   = desc.buffer.length;
        packet.hash     = desc.metadata.hash;
        packet.queueId  = desc.metadata.queueId;
        packet.offloads = desc.metadata.offloads;

        process(std::move(packet));
        ++sent;
    }
    return sent;
}

bool VNic::validate(const IncomingPacket& packet) const {
    return true; //TODO
}

bool VNic::l2Filter(const IncomingPacket& packet) const {
    return true; //TODO
}

bool VNic::softOffloads(IncomingPacket& packet) {
    return true; // TODO
}

void VNic::distribute(IncomingPacket&& packet) {
    assert(!rxQueues_.empty());
    const auto& rssConfig{config_.rss};

    if (rssConfig.enabled) {
        assert(!reta_.empty() && "RETA is not initialized!");
        assert((reta_.size() & (reta_.size() - 1)) == 0 && "RETA size must be a power of 2!");
        const auto hash{rss::soft::calc_hash(packet.get5Tuple(), rssConfig.key, rssConfig.hf, rssConfig.protocol)};
        packet.queueId = reta_[hash & (reta_.size() - 1)];
    } else {
        packet.queueId = 0;
    }

    const auto bytes{packet.length};
    assert(packet.queueId < rxQueues_.size());
    if (rxQueues_[packet.queueId].put(std::move(packet))) {
        stats_.fetchProcessedPackets();
        stats_.fetchProcessedBytes(bytes);
    } else {
        stats_.fetchDroppedPackets();
        stats_.fetchDroppedBytes(bytes);
    }
}

bool VNic::upLink() {
    return link_.up();
}

bool VNic::downLink() {
    return link_.down();
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

    std::vector<IncomingPacket> packets;
    packets.reserve(64);

    while (getState() == State::Started) {
        packets.clear();
        (void)link_.get(packets); // blocking call; разблокируется wakeup() при stop()
        if (packets.empty()) {
            continue;  // вышли по wakeup(), но без пакета — перепроверяем состояние
        }

        for (auto& packet : packets) {
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
        }
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

bool VNic::stopImpl() {
    link_.wakeup();
    return stopQueues();
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
