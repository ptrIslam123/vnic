#include "vnic/vnic.h"
#include "clock/clock.h"
#include "parser/parser.h"
#include "utils/scoped_guard.h"

#include <cassert>

namespace {

constexpr std::size_t kNicRssMinKeyLen{40};
constexpr std::uint16_t kVlanIdMax{0x0FFFu};

} // namespace

namespace vnic {

ErrorCode VNic::Config::Rss::validate() const noexcept {
    if (!enabled) {
        return ErrorCode::None;
    }
    if (key.size() < kNicRssMinKeyLen) {
        return ErrorCode::ConfigRssKeyTooShort;
    }
    if (!reta.empty() && (reta.size() & (reta.size() - 1)) != 0) {
        return ErrorCode::ConfigRssRetaSizeNotPowerOfTwo;
    }
    return ErrorCode::None;
}

bool VNic::Config::Rss::isValid() const noexcept { return validate() == ErrorCode::None; }

ErrorCode VNic::Config::validate() const noexcept {
    if (mtu == 0) {
        return ErrorCode::ConfigMtuInvalid;
    }
    if (const auto code = link.validate(); code != ErrorCode::None) {
        return code;
    }
    if (const auto code = rss.validate(); code != ErrorCode::None) {
        return code;
    }
    if (rss.enabled) {
        if (rxQueueCount == 0) {
            return ErrorCode::ConfigQueueCountInvalid;
        }
        for (const auto queueId : rss.reta) {
            if (queueId >= rxQueueCount) {
                return ErrorCode::ConfigRssRetaQueueOutOfRange;
            }
        }
    }
    for (const auto vlanId : vlanAllowList) {
        if (vlanId > kVlanIdMax) {
            return ErrorCode::ConfigVlanInvalid;
        }
    }
    if (!macAddr.empty() && !validate::ParseMac(macAddr).has_value()) {
        return ErrorCode::ConfigMacInvalid;
    }
    return ErrorCode::None;
}

bool VNic::Config::isValid() const noexcept { return validate() == ErrorCode::None; }

void VNic::poll(std::stop_token stopToken) {
    std::vector<std::byte> buffer;
    buffer.reserve(kPacketBufferSize);

    while (!stopToken.stop_requested()) {
        if (!link_.isUp()) [[unlikely]] {
            std::this_thread::sleep_for(std::chrono::milliseconds{100});
            continue;
        }

        buffer.clear();
        auto taken{link_.takeRx(buffer, std::chrono::milliseconds{100})};
        if (!taken) [[unlikely]] {
            if (taken.error() != ErrorCode::Timeout) {
                stats_.fetchErrors();
            }
            continue;
        }
        const auto n{*taken};
        if (n == 0) {
            continue;
        }

        std::span<const std::byte> packet{buffer.data(), n};
        Parser parser;
        if (auto parsed = parser.parse(packet); !parsed) [[unlikely]] {
            stats_.fetchErrors();
            stats_.fetchDroppedPackets();
            stats_.fetchDroppedBytes(n);
            continue;
        }

        if (auto valid = validate(packet, parser); !valid) [[unlikely]] {
            stats_.fetchErrors();
            stats_.fetchDroppedPackets();
            stats_.fetchDroppedBytes(n);
            continue;
        }

        (void)distribute(parser, packet);
    }
}

std::expected<void, ErrorCode> VNic::distribute(const Parser& parser, std::span<const std::byte> packet) {
    if (rxQueues_.empty()) [[unlikely]] {
        stats_.fetchDroppedPackets();
        stats_.fetchDroppedBytes(packet.size());
        return std::unexpected(ErrorCode::QueueNotConfigured);
    }

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
    const auto queueId{desc.queueId};
    assert(queueId < rxQueues_.size());
    if (auto pushed = rxQueues_[queueId].put(std::move(desc), packet); !pushed) [[unlikely]] {
        stats_.fetchDroppedPackets();
        stats_.fetchDroppedBytes(bytes);
        return std::unexpected(pushed.error());
    }

    stats_.fetchProcessedPackets();
    stats_.fetchProcessedBytes(bytes);
    return {};
}

std::expected<std::size_t, ErrorCode> VNic::tx(std::uint16_t queueId, std::span<PacketDescriptor> descs) {
    if (queueId >= txQueues_.size()) [[unlikely]] {
        return std::unexpected(ErrorCode::QueueInvalidId);
    }

    std::size_t transmitted{0};
    ErrorCode error{ErrorCode::None};
    for (auto&& desc : descs) {
        const auto length{desc.totalLength};
        if (auto pushed = txQueues_[queueId].put(std::move(desc)); !pushed) [[unlikely]] {
            error = pushed.error();
            stats_.fetchTxDroppedPackets();
            stats_.fetchTxDroppedBytes(length);
            break;
        }
        stats_.fetchTransmittedPackets();
        stats_.fetchTransmittedBytes(length);
        ++transmitted;
    }

    if (transmitted == 0 && error != ErrorCode::None) {
        return std::unexpected(error);
    }
    return transmitted;
}

std::expected<std::size_t, ErrorCode> VNic::rx(const std::uint16_t queueId, std::vector<PacketDescriptor>& descs) {
    if (queueId >= rxQueues_.size()) [[unlikely]] {
        return std::unexpected(ErrorCode::QueueInvalidId);
    }
    return rxQueues_[queueId].take(descs);
}

std::expected<void, ErrorCode> VNic::freeRx(const std::uint16_t queueId, std::span<const PacketDescriptor> descs) {
    if (queueId >= rxQueues_.size()) [[unlikely]] {
        return std::unexpected(ErrorCode::QueueInvalidId);
    }
    return rxQueues_[queueId].free(descs);
}

std::expected<void, ErrorCode> VNic::validate(std::span<const std::byte> packet, const Parser& parser) {
    if (!validate::CheckFrameSize(packet, parser, kMinFrameSize, config_.mtu)) {
        const auto l2HeaderBytes{parser.l2Offset() != 0 ? parser.l2Offset() : sizeof(ethhdr)};
        return std::unexpected(packet.size() > l2HeaderBytes + config_.mtu ? ErrorCode::ValidationFrameTooLong
                                                                           : ErrorCode::ValidationFrameTooShort);
    }
    if (!validate::CheckFcs(packet)) {
        return std::unexpected(ErrorCode::ValidationFcsInvalid);
    }
    if (!validate::CheckDstMac(parser, macAddr_, config_.promiscuousEnabled)) {
        return std::unexpected(ErrorCode::ValidationDstMacMismatch);
    }
    if (!validate::CheckVlanFilter(parser, config_.vlanAllowList)) {
        return std::unexpected(ErrorCode::ValidationVlanRejected);
    }
    if (!validate::CheckIpTotalLength(packet, parser)) {
        return std::unexpected(ErrorCode::ValidationIpTotalLengthInvalid);
    }
    if (!validate::CheckIpv4Checksum(packet, parser)) {
        return std::unexpected(ErrorCode::ValidationIpv4ChecksumInvalid);
    }
    return {};
}

std::expected<void, ErrorCode> VNic::initReta(Config::Rss::Reta& reta) {
    assert(config_.rss.enabled);

    if (config_.rxQueueCount == 0) {
        return std::unexpected(ErrorCode::ConfigQueueCountInvalid);
    }

    if (!config_.rss.reta.empty()) {
        const auto size = config_.rss.reta.size();
        if ((size & (size - 1)) != 0) {
            return std::unexpected(ErrorCode::ConfigRssRetaSizeNotPowerOfTwo);
        }

        for (auto queueId : config_.rss.reta) {
            if (queueId >= config_.rxQueueCount) {
                return std::unexpected(ErrorCode::ConfigRssRetaQueueOutOfRange);
            }
        }

        reta = config_.rss.reta;
        return {};
    }

    // Round-Robin
    constexpr auto RETA_SIZE{512};
    reta.resize(RETA_SIZE);
    for (std::uint16_t i{0}; i < RETA_SIZE; ++i) {
        reta[i] = i % config_.rxQueueCount;
    }
    return {};
}

std::expected<void, ErrorCode> VNic::configureImpl(const Config& config) {
    if (const auto code = config.validate(); code != ErrorCode::None) [[unlikely]] {
        return std::unexpected(code);
    }

    config_ = config;

    utils::ScopedGuard rollback{[this] {
        rxQueues_.clear();
        txQueues_.clear();
        reta_.write(Config::Rss::Reta{});
        config_  = Config{};
        macAddr_ = {};
    }};

    // Пустой macAddr допустим (оставляем нулевой адрес), но непустой и
    // нераспознанный — ошибка конфигурации: иначе молча получим нулевой MAC и
    // при выключенном promiscuous собственные unicast-кадры будут дропаться.
    macAddr_ = {};
    if (!config_.macAddr.empty()) {
        const auto mac = validate::ParseMac(config_.macAddr);
        if (!mac) [[unlikely]] {
            return std::unexpected(ErrorCode::ConfigMacInvalid);
        }
        macAddr_ = *mac;
    }

    if (auto linked = configureLink(); !linked) [[unlikely]] {
        return std::unexpected(linked.error() != ErrorCode::None ? linked.error() : ErrorCode::LinkConfigureFailed);
    }

    if (auto configured = configureQueues(); !configured) [[unlikely]] {
        return configured;
    }

    if (auto configured = configureRss(); !configured) [[unlikely]] {
        return configured;
    }

    rollback.cancel();
    return {};
}

std::expected<void, ErrorCode> VNic::configureQueues() {
    rxQueues_.clear();
    rxQueues_.resize(config_.rxQueueCount);

    txQueues_.clear();
    txQueues_.resize(config_.txQueueCount);
    return {};
}

std::expected<void, ErrorCode> VNic::configureRss() {
    const auto& rssConfig{config_.rss};
    if (!rssConfig.enabled) {
        return {};
    }

    if (const auto code = rssConfig.validate(); code != ErrorCode::None) {
        return std::unexpected(code);
    }

    Config::Rss::Reta reta;
    if (auto initialized = initReta(reta); !initialized) [[unlikely]] {
        return initialized;
    }
    return updateReta(std::move(reta));
}

std::expected<void, ErrorCode> VNic::configureLink() { return link_.configure(config_.link); }

std::expected<void, ErrorCode> VNic::configureImpl(const std::uint16_t queueId, const RxQueue::Config& config) {
    if (queueId >= rxQueues_.size()) [[unlikely]] {
        return std::unexpected(ErrorCode::QueueInvalidId);
    }
    if (!config.isValid()) [[unlikely]] {
        return std::unexpected(ErrorCode::ConfigQueueSizeInvalid);
    }
    return rxQueues_[queueId].configure(config);
}

std::expected<void, ErrorCode> VNic::configureImpl(const std::uint16_t queueId, const TxQueue::Config& config) {
    if (queueId >= txQueues_.size()) [[unlikely]] {
        return std::unexpected(ErrorCode::QueueInvalidId);
    }
    if (!config.isValid()) [[unlikely]] {
        return std::unexpected(ErrorCode::QueueInvalidConfig);
    }
    return txQueues_[queueId].configure(config, link_);
}

std::expected<void, ErrorCode> VNic::startImpl() {
    if (auto started = startQueues(); !started) [[unlikely]] {
        return started;
    }

    utils::ScopedGuard rollback{[this] { (void)stopQueues(); }};
    try {
        thread_ = std::jthread{[this](std::stop_token st) { poll(st); }};
    } catch (...) {
        return std::unexpected(ErrorCode::ThreadCreateFailed);
    }
    rollback.cancel();
    return {};
}

std::expected<void, ErrorCode> VNic::stopImpl() {
    thread_.request_stop();
    if (thread_.joinable()) {
        thread_.join();
    }
    return stopQueues();
}

std::expected<void, ErrorCode> VNic::startQueues() {
    std::size_t startedRx{0};
    std::size_t startedTx{0};
    utils::ScopedGuard rollback{[this, &startedRx, &startedTx] {
        for (std::size_t i{0}; i < startedRx; ++i) {
            (void)rxQueues_[i].stop();
        }
        for (std::size_t i{0}; i < startedTx; ++i) {
            (void)txQueues_[i].stop();
        }
    }};

    for (auto& queue : rxQueues_) {
        if (auto started = queue.start(); !started) [[unlikely]] {
            return std::unexpected(started.error() != ErrorCode::None ? started.error() : ErrorCode::QueueStartFailed);
        }
        ++startedRx;
    }
    for (auto& queue : txQueues_) {
        if (auto started = queue.start(); !started) [[unlikely]] {
            return std::unexpected(started.error() != ErrorCode::None ? started.error() : ErrorCode::QueueStartFailed);
        }
        ++startedTx;
    }

    rollback.cancel();
    return {};
}

std::expected<void, ErrorCode> VNic::stopQueues() {
    ErrorCode firstError{ErrorCode::None};
    for (auto& queue : rxQueues_) {
        if (auto stopped = queue.stop(); !stopped) [[unlikely]] {
            if (firstError == ErrorCode::None) {
                firstError = stopped.error() != ErrorCode::None ? stopped.error() : ErrorCode::QueueStopFailed;
            }
        }
    }
    for (auto& queue : txQueues_) {
        if (auto stopped = queue.stop(); !stopped) [[unlikely]] {
            if (firstError == ErrorCode::None) {
                firstError = stopped.error() != ErrorCode::None ? stopped.error() : ErrorCode::QueueStopFailed;
            }
        }
    }
    if (firstError != ErrorCode::None) {
        return std::unexpected(firstError);
    }
    return {};
}

std::expected<void, ErrorCode> VNic::updateReta(Config::Rss::Reta&& reta) {
    if (reta.empty()) {
        return std::unexpected(ErrorCode::RssRetaEmpty);
    }

    const auto size{reta.size()};
    if ((size & (size - 1)) != 0) {
        return std::unexpected(ErrorCode::RssRetaSizeInvalid);
    }

    for (const auto queueId : reta) {
        if (queueId >= config_.rxQueueCount) {
            return std::unexpected(ErrorCode::RssRetaQueueOutOfRange);
        }
    }

    reta_.write(std::move(reta));
    return {};
}

std::expected<void, ErrorCode> VNic::updateReta(const Config::Rss::Reta& reta) {
    return updateReta(Config::Rss::Reta{reta});
}

VNic::~VNic() {
    if (thread_.joinable()) {
        thread_.request_stop();
        thread_.join();
    }
    (void)stopQueues();
}

void VNic::resetStats() { stats_.reset(); }

Link& VNic::getLink() { return link_; }

const Link& VNic::getLink() const { return link_; }

const stats::Stats& VNic::getStats() const { return stats_; }

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
