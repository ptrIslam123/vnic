#pragma once

#include "queue/rx_queue.h"
#include "queue/tx_queue.h"
#include "packet/incoming_packet.h"
#include "channel/link.h"
#include "rss/hash.h"
#include "utils/state_ful.h"

#include <vector>
#include <cstdint>

namespace vnic {

class VNic final : public utils::Stateful<VNic> {
public:
    VNic() = default;
    VNic(const VNic&) = delete;
    VNic(VNic&&) = delete;
    VNic& operator=(const VNic&) = delete;
    VNic& operator=(VNic&&) = delete;

    enum class ErrorCode {

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

    bool upLink();
    bool downLink();

    void process(IncomingPacket&& packet);
    std::uint64_t rx(std::vector<PacketDescriptor>& descs);
    std::uint64_t tx(std::vector<PacketDescriptor>& descs);
    void freeRx(std::vector<PacketDescriptor>& descs);
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

    bool validate(const IncomingPacket& packet) const;
    bool l2Filter(const IncomingPacket& packet) const;
    bool softOffloads(IncomingPacket& packet);

    void distribute(IncomingPacket&& packet);

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
    enum ErrorCode eCode_;


    // interfaces
    // DMA controllers
    // Registers for control
    // Mac address
    // Statistics
};

} // namespace vnic
