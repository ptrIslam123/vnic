#pragma once

#include "queue/queue.h"
#include "packet/packet.h"
#include "channel/link.h"
#include "rss/hash.h"

#include <vector>
#include <cstdint>

namespace vnic {

class VNic final {
public:
    enum class ErrorCode {

    };

    enum class State : uint8_t {
        Started,
        Stopped,
    };

    struct Config {
        bool promiscuousEnabled;
        std::uint32_t mtu;
        std::uint16_t rxQueueCount;
        std::uint16_t txQueueCount;

        struct Rss {
            using Reta = std::vector<std::uint16_t>;
            using Key = std::vector<std::uint8_t>;

            bool enabled;
            rss::HashFunc hf;
            rss::Protocol protocol;
            Key key;
            Reta reta;
        } rss;

        Link::Config link;

        struct VlanConfig {
            bool enabled;
            bool strip;      // Удалять VLAN тег
            bool insert;     // Вставлять VLAN тег
            std::uint16_t defaultVlanId = 0;
            std::vector<std::uint16_t> filterIds;  // Разрешенные VLAN ID
        } vlan;

        struct OffloadConfig {
            bool rxChecksumIp;
            bool rxChecksumTcp;
            bool rxChecksumUdp;
            bool txChecksumIp;
            bool txChecksumTcp;
            bool txChecksumUdp;
            bool tso;       // TCP Segmentation Offload
            bool lro;       // Large Receive Offload
        } offloads;
    };

    bool updateReta(Config::Rss::Reta&& reta);
    bool updateReta(const Config::Rss::Reta& reta);

    bool configure(const Config& config);
    bool configureQueue(std::uint16_t queueId, const RxQueue::Config& config);
    bool configureQueue(std::uint16_t queueId, const TxQueue::Config& config);

    bool upLink();
    bool downLink();

    bool start();
    bool stop();

    void rx(Packet&& packet);
    void resetStats();

    const RxQueue& getRxQueue(std::uint16_t queueId) const;
    const TxQueue& getTxQueue(std::uint16_t queueId) const;
    const Config& getConfig() const;
    const Config::Rss::Reta& getReta() const;
    const stats::Stats& getStats() const;
    enum ErrorCode getErrorCode() const;
    enum State getState() const;

private:
    bool initReta();
    bool configureRss();
    bool configureQueues();
    bool configureLink();

    bool validate(const Packet& packet) const;
    bool l2Filter(const Packet& packet) const;
    bool softOffloads(Packet& packet);

    void distribute(Packet&& packet);

    bool startQueues();
    bool stopQueues();

    void setState(State state);

    Link link_;
    Config::Rss::Reta reta_;
    std::vector<RxQueue> rxQueues_;
    std::vector<TxQueue> txQueues_;
    stats::Stats stats_;

    std::vector<RxQueue::Config> rxQueueConfigs_;
    std::vector<TxQueue::Config> txQueueConfigs_;
    Config config_;
    enum State state_;
    enum ErrorCode eCode_;


    // interfaces
    // DMA controllers
    // Registers for control
    // Mac address
    // Statistics
};

} // namespace vnic
