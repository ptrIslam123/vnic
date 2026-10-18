#include "vnic/vnic.h"
#include "parser/parser.h"
#include "clock/clock.h"

#include <cassert>

namespace {} // namespace

namespace vnic {

void VNic::poll(std::stop_token stopToken) {
    std::vector<std::byte> buffer;
    buffer.reserve(PACKET_BUFFER_SIZE);

    while (!stopToken.stop_requested()) {
        if (!link_.isUp()) [[unlikely]] {
            std::this_thread::sleep_for(std::chrono::milliseconds{100});
            continue;
        }

        buffer.clear();
        const auto n{link_.takeRx(buffer, std::chrono::milliseconds{100})};
        if (n == 0) { // by timeout
            continue;
        }

        std::span<const std::byte> packet{buffer.data(), n};
        Parser parser;
        if (!parser.parse(packet)) [[unlikely]] {
            stats_.fetchErrors();
            stats_.fetchDroppedPackets();
            stats_.fetchDroppedBytes(n);
            continue;
        }

        if (!validate(packet, parser)) [[unlikely]] {
            stats_.fetchErrors();
            stats_.fetchDroppedPackets();
            stats_.fetchDroppedBytes(n);
            continue;
        }

        distribute(parser, packet);
    }
}

void VNic::distribute(const Parser& parser, std::span<const std::byte> packet) {
    assert(!rxQueues_.empty());
    PacketDescriptor desc;
    desc.timestamp = clock::monotonic::Now();
    desc.vlanId    = parser.vlanId();
    const auto& rssConfig{config_.rss};

    // RSS применим только к IP-потокам: у ARP и прочих не-IP кадров нет
    // 5-tuple. Как и реальный NIC, такие кадры не хэшируем, а направляем в
    // очередь по умолчанию (0).
    const auto l2{parser.l2Protocol()};
    const auto rssHashable{l2 == Parser::L2Protocol::Ipv4 || l2 == Parser::L2Protocol::Ipv6};

    if (rssConfig.enabled && rssHashable) {
        desc.hash =
            rss::soft::CalcHash(Get5Tuple(parser), rssConfig.key, rssConfig.hf, rssConfig.protocol, rssConfig.symmetry);
        // Читаем один элемент под shared_lock, не копируя всю таблицу.
        desc.queueId = reta_.read([&](const Config::Rss::Reta& reta) -> std::uint16_t {
            if (reta.empty()) {
                return 0;
            }
            assert((reta.size() & (reta.size() - 1)) == 0 && "RETA size must be a power of 2!");
            return reta[desc.hash & (reta.size() - 1)];
        });
    } else {
        desc.hash    = 0;
        desc.queueId = 0;
    }

    const auto bytes{packet.size()};
    assert(desc.queueId < rxQueues_.size());
    if (rxQueues_[desc.queueId].put(std::move(desc), packet)) {
        stats_.fetchProcessedPackets();
        stats_.fetchProcessedBytes(bytes);
    } else {
        stats_.fetchDroppedPackets();
        stats_.fetchDroppedBytes(bytes);
    }
}

std::size_t VNic::tx(std::uint16_t queueId, std::span<PacketDescriptor> descs) {
    if (queueId >= txQueues_.size()) {
        return 0;
    }

    std::size_t transmitted{0};
    for (auto&& desc : descs) {
        const auto length{desc.totalLength};
        if (!txQueues_[queueId].put(std::move(desc))) [[unlikely]] {
            stats_.fetchTxDroppedPackets();
            stats_.fetchTxDroppedBytes(length);
            break;
        }
        stats_.fetchTransmittedPackets();
        stats_.fetchTransmittedBytes(length);
        ++transmitted;
    }
    return transmitted;
}

// Возвращает число изъятых дескрипторов (>= 0) либо -1, если queueId неверный.
std::int64_t VNic::rx(const std::uint16_t queueId, std::vector<PacketDescriptor>& descs) {
    if (queueId >= rxQueues_.size()) {
        return -1;
    }
    return static_cast<std::int64_t>(rxQueues_[queueId].take(descs));
}

bool VNic::freeRx(const std::uint16_t queueId, std::span<const PacketDescriptor> descs) {
    if (queueId >= rxQueues_.size()) {
        return false;
    }
    return rxQueues_[queueId].free(descs);
}

bool VNic::validate(std::span<const std::byte> packet, const Parser& parser) const {
    return (validate::CheckFrameSize(packet, parser, MIN_FRAME_SIZE, config_.mtu) && validate::CheckFcs(packet) &&
            validate::CheckDstMac(parser, macAddr_, config_.promiscuousEnabled) &&
            validate::CheckVlanFilter(parser, config_.vlanAllowList) && validate::CheckIpTotalLength(packet, parser) &&
            validate::CheckIpv4Checksum(packet, parser));
}

bool VNic::initReta(Config::Rss::Reta& reta) {
    assert(config_.rss.enabled);

    if (!config_.rss.reta.empty()) {
        const auto size = config_.rss.reta.size();
        if ((size & (size - 1)) != 0) {
            return false; // RETA size must be a power of 2!
        }

        for (auto queueId : config_.rss.reta) {
            if (queueId >= config_.rxQueueCount) {
                return false; // invalid queue idx
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
    eCode_  = ErrorCode::None;

    // Пустой macAddr допустим (оставляем нулевой адрес), но непустой и
    // нераспознанный — ошибка конфигурации: иначе молча получим нулевой MAC и
    // при выключенном promiscuous собственные unicast-кадры будут дропаться.
    macAddr_ = {};
    if (!config_.macAddr.empty()) {
        const auto mac = validate::ParseMac(config_.macAddr);
        if (!mac) [[unlikely]] {
            eCode_ = ErrorCode::Error;
            return false;
        }
        macAddr_ = *mac;
    }

    if (!configureLink()) [[unlikely]] {
        eCode_ = ErrorCode::Error;
        return false;
    }

    if (!configureQueues()) [[unlikely]] {
        eCode_ = ErrorCode::Error;
        return false;
    }

    if (!configureRss()) [[unlikely]] {
        eCode_ = ErrorCode::Error;
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

bool VNic::configureLink() { return link_.configure(config_.link); }

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
    return txQueues_[queueId].configure(config, link_);
}

bool VNic::startImpl() {
    if (!startQueues()) [[unlikely]] {
        return false;
    }

    thread_ = std::jthread{[this](std::stop_token st) { poll(st); }};
    return true;
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
    if (reta.empty()) {
        return false;
    }

    const auto size{reta.size()};
    if ((size & (size - 1)) != 0) {
        return false; // размер RETA должен быть степенью двойки
    }

    for (const auto queueId : reta) {
        if (queueId >= config_.rxQueueCount) {
            return false; // некорректный индекс очереди
        }
    }

    reta_.write(std::move(reta));
    return true;
}

bool VNic::updateReta(const Config::Rss::Reta& reta) { return updateReta(Config::Rss::Reta{reta}); }

void VNic::resetStats() { stats_.reset(); }

Link& VNic::getLink() { return link_; }

const Link& VNic::getLink() const { return link_; }

const stats::Stats& VNic::getStats() const { return stats_; }

enum VNic::ErrorCode VNic::getErrorCode() const { return eCode_; }

VNic::Config::Rss::Reta VNic::getReta() const { return reta_.snapshot(); }

const VNic::Config& VNic::getConfig() const { return config_; }

const RxQueue* VNic::getRxQueue(std::uint16_t queueId) const {
    if (queueId >= rxQueues_.size()) {
        return nullptr;
    }
    return &rxQueues_[queueId];
}

const TxQueue* VNic::getTxQueue(std::uint16_t queueId) const {
    if (queueId >= txQueues_.size()) {
        return nullptr;
    }
    return &txQueues_[queueId];
}

} // namespace vnic
