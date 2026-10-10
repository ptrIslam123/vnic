#pragma once

#include "link/link.h"
#include "queue/rx_queue.h"
#include "queue/tx_queue.h"
#include "rss/hash.h"
#include "utils/state_ful.h"

#include <vector>
#include <span>
#include <thread>

#include <cstdint>

namespace vnic {

class Parser;

class VNic final : public utils::Stateful<VNic> {
public:
    VNic() = default;
    VNic(const VNic&) = delete;
    VNic(VNic&&) = delete;
    VNic& operator=(const VNic&) = delete;
    VNic& operator=(VNic&&) = delete;

    enum class ErrorCode {
        // TODO
    };

    struct Config {
        bool promiscuousEnabled;
        std::uint32_t mtu;
        std::uint16_t rxQueueCount;
        std::uint16_t txQueueCount;

        struct Rss {
            using Reta = std::vector<std::uint16_t>;
            using Key = std::vector<std::uint8_t>;

            bool enabled{false};
            rss::HashFunc hf{};
            rss::Protocol protocol{};
            rss::SymmetryMode symmetry{};
            Key key{};
            Reta reta{};
        } rss;

        std::string macAddr;
        std::vector<std::string> ipAddrs;
        Link::Config link;
    };

    const Link& getLink() const;
    Link& getLink();
    bool updateReta(Config::Rss::Reta&& reta);
    bool updateReta(const Config::Rss::Reta& reta);

    bool tx(std::uint16_t queueId, std::span<const PacketDescriptor> descs);
    std::int64_t rx(std::uint16_t queueId, std::vector<PacketDescriptor>& descs);
    bool freeRx(std::uint16_t queueId, std::span<const PacketDescriptor> descs);

    void resetStats();

    const RxQueue& getRxQueue(std::uint16_t queueId) const;
    const TxQueue& getTxQueue(std::uint16_t queueId) const;
    const Config& getConfig() const;
    const stats::Stats& getStats() const;
    const Config::Rss::Reta& getReta() const;
    enum ErrorCode getErrorCode() const;

private:
    friend utils::Stateful<VNic>;

    bool startImpl();
    bool stopImpl();

    bool configureImpl(const Config& config);
    bool configureImpl(std::uint16_t queueId, const RxQueue::Config& config);
    bool configureImpl(std::uint16_t queueId, const TxQueue::Config& config);

    bool initReta(Config::Rss::Reta& reta);
    bool configureRss();
    bool configureQueues();
    bool configureLink();

    bool validate(const Parser& parser) const;

    void distribute(const Parser& parser, std::span<const std::byte> payload);
    void poll(std::stop_token stopToken);

    bool startQueues();
    bool stopQueues();

    Link link_;
    Config::Rss::Reta reta_;
    std::vector<RxQueue> rxQueues_;
    std::vector<TxQueue> txQueues_;
    stats::Stats stats_;

    std::vector<RxQueue::Config> rxQueueConfigs_;
    std::vector<TxQueue::Config> txQueueConfigs_;
    Config config_;
    enum ErrorCode eCode_;

    std::jthread thread_;
};

} // namespace vnic
