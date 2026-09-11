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
        bool PromiscuousEnabled;
        std::uint32_t mtu;
        std::uint16_t rxQueueCount;
        std::uint16_t txQueueCount;

        struct RssConfig {
            using Reta = std::vector<std::uint16_t>;
            using Key = std::vector<std::uint8_t>;

            bool enabled;
            rss::HashFunc hf;
            rss::Protocol protocol;
            Key key;
            Reta reta;
        } rss;

        struct LinkConfig {
            bool up;
            bool fullDuplex;
            std::uint32_t speedMbps;
        } link;

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

    bool updateReta(Config::RssConfig::Reta&& reta);
    bool updateReta(const Config::RssConfig::Reta& reta);
    bool configure(const Config& config);
    bool configure(std::uint16_t queueId, const RxQueue::Config& config);
    bool configure(std::uint16_t queueId, const TxQueue::Config& config);
    bool start();
    bool stop();
    void rx(Packet&& packet);
    void resetStats();

    const RxQueue& getRxQueue(std::uint16_t queueId) const;
    const TxQueue& getTxQueue(std::uint16_t queueId) const;
    const Config& getConfig() const;
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
    void notify_rx();

    bool startQueues();
    bool stopQueues();

    Link link_;
    Config::RssConfig::Reta reta_;
    std::vector<RxQueue> rxQueues_;
    std::vector<TxQueue> txQueues_;
    stats::Stats stats_;

    std::vector<RxQueue::Config> rxQueueConfigs_;
    std::vector<TxQueue::Config> txQueueConfigs_;
    Config config_;
    ErrorCode eCode_;


    // interfaces
    // DMA controllers
    // Registers for control
    // Mac address
    // Statistics
};


// // Предварительные объявления
// class RxQueue;
// class TxQueue;
// class FlowTable;
// class StatsManager;
// class PciConfigSpace;
// class NicConfig;
// class FlowRule;
// class NicStats;
// class LinkStats;
// class MacAddr;
// class RxQueueConfig;
// class TxQueueConfig;
// class FlowAction;
// class RssConfig;

// class VNic final {
// public:
//     // --- Конструкторы и деструкторы ---
//     VNic(uint16_t port_id, const std::string& name);
//     ~VNic();

//     // --- Базовое управление устройством ---
//     bool configure(uint32_t nb_rx_queues, uint32_t nb_tx_queues,
//                    const NicConfig& config);
//     bool start();
//     void stop();
//     void close();
//     void reset();

//     // --- Управление очередями ---
//     bool setupRxQueue(uint16_t queue_id, uint32_t nb_descs,
//                       const RxQueueConfig& config);
//     bool setupTxQueue(uint16_t queue_id, uint32_t nb_descs,
//                       const TxQueueConfig& config);
//     bool startRxQueue(uint16_t queue_id);
//     bool stopRxQueue(uint16_t queue_id);
//     void releaseRxQueue(uint16_t queue_id);
//     void releaseTxQueue(uint16_t queue_id);

//     // --- Основные операции приема/передачи ---
//     uint16_t rxBurst(uint16_t queue_id, void** rx_pkts, uint16_t nb_pkts);
//     uint16_t txBurst(uint16_t queue_id, void** tx_pkts, uint16_t nb_pkts);
//     uint16_t txPrepare(uint16_t queue_id, void** tx_pkts, uint16_t nb_pkts);

//     // --- Управление потоками (Flow Steering) ---
//     int flowCreate(const FlowRule& rule, FlowAction* action);
//     int flowDestroy(int flow_id);
//     int flowValidate(const FlowRule& rule);
//     void flowFlush();

//     // --- RSS (Receive Side Scaling) ---
//     bool configureRss(const RssConfig& config);
//     uint32_t calculateRssHash(const void* packet_data, uint16_t len);

//     // --- Управление MAC-адресами ---
//     bool setMacAddress(const MacAddr& addr);
//     MacAddr getMacAddress() const;
//     bool addMacAddress(const MacAddr& addr);
//     bool removeMacAddress(const MacAddr& addr);

//     // --- VLAN ---
//     bool enableVlanFilter(uint16_t vlan_id);
//     bool disableVlanFilter(uint16_t vlan_id);
//     bool setVlanStrip(bool enable);
//     bool setVlanInsert(uint16_t vlan_id);

//     // --- Оффлоады ---
//     struct OffloadCapabilities {
//         bool checksum_ip;
//         bool checksum_tcp;
//         bool checksum_udp;
//         bool tso;
//         bool lro;
//         bool vlan_strip;
//         bool vlan_insert;
//         bool tunnel_vxlan;
//         bool tunnel_gre;
//     };
//     OffloadCapabilities getOffloadCaps() const;
//     void enableOffload(uint32_t offload_mask);
//     void disableOffload(uint32_t offload_mask);

//     // --- SR-IOV (виртуализация) ---
//     bool enableSRIOV(uint16_t num_vfs);
//     bool disableSRIOV();
//     bool configureVf(uint16_t vf_id, const VfConfig& config);
//     bool getVfInfo(uint16_t vf_id, VfInfo* info);
//     bool setVfRate(uint16_t vf_id, uint64_t rate_limit);

//     // --- QoS (Traffic Management) ---
//     bool configureQoS(const QosConfig& config);
//     bool createHierarchicalQueue(const HqosNode& node);
//     bool deleteHierarchicalQueue(uint32_t node_id);
//     bool setQueueRate(uint32_t node_id, uint64_t rate_bps);

//     // --- Управление линком ---
//     bool setLinkUp();
//     bool setLinkDown();
//     bool setLinkSpeed(uint32_t speed_mbps);
//     LinkStatus getLinkStatus() const;

//     // --- Статистика ---
//     void resetStats();
//     NicStats getStats() const;
//     std::map<std::string, uint64_t> getExtendedStats() const;

//     // --- Управление прерываниями ---
//     bool setupInterrupts(const InterruptConfig& config);
//     void triggerInterrupt(uint16_t queue_id, InterruptType type);

//     // --- DMA и управление памятью ---
//     bool dmaMapMemory(void* addr, size_t len);
//     bool dmaUnmapMemory(void* addr);
//     void* dmaAllocateBuffer(size_t len);

//     // --- Управление питанием (Advanced) ---
//     bool setPowerState(PowerState state);
//     PowerState getPowerState() const;

//     // --- Мониторинг и отладка ---
//     void dumpDebugInfo() const;
//     bool enableDebugMode(bool enable);
//     void injectError(ErrorType type);

// private:
//     // === АППАРАТНЫЕ КОМПОНЕНТЫ ===

//     // 1. Базовые параметры устройства
//     uint16_t port_id_;
//     std::string device_name_;
//     MacAddr mac_addr_;
//     LinkStatus link_status_;
//     PowerState power_state_;

//     // 2. Очереди
//     std::vector<std::unique_ptr<RxQueue>> rx_queues_;
//     std::vector<std::unique_ptr<TxQueue>> tx_queues_;

//     // 3. Управление потоками и RSS
//     std::unique_ptr<FlowTable> flow_table_;
//     RssConfig rss_config_;

//     // 4. Конфигурационные пространства (PCIe)
//     std::unique_ptr<PciConfigSpace> pci_config_;

//     // 5. Управление виртуализацией
//     struct SRIOVState {
//         bool enabled;
//         uint16_t num_vfs;
//         std::vector<VfConfig> vf_configs;
//         std::vector<std::unique_ptr<Nic>> vfs; // Вложенные экземпляры
//     } sriov_state_;

//     // 6. QoS (Hierarchical Quality of Service)
//     struct QoSState {
//         bool enabled;
//         std::vector<HqosNode> hqos_tree_;
//         std::map<uint32_t, uint64_t> node_rates_;
//     } qos_state_;

//     // 7. Оффлоады
//     OffloadCapabilities offload_caps_;
//     uint32_t enabled_offloads_;

//     // 8. Управление VLAN
//     std::map<uint16_t, bool> vlan_filter_;
//     bool vlan_strip_enabled_;
//     bool vlan_insert_enabled_;
//     uint16_t default_vlan_id_;

//     // 9. Прерывания
//     InterruptConfig interrupt_config_;
//     std::atomic<bool> interrupt_pending_;

//     // 10. DMA менеджер
//     struct DmaRegion {
//         void* addr;
//         size_t len;
//         bool mapped;
//     };
//     std::vector<DmaRegion> dma_regions_;

//     // 11. Статистика
//     NicStats stats_;
//     std::map<std::string, uint64_t> extended_stats_;
//     std::atomic<bool> debug_mode_;

//     // 12. Состояние устройства
//     enum class DeviceState {
//         UNCONFIGURED,
//         CONFIGURED,
//         RUNNING,
//         STOPPED,
//         ERROR
//     };
//     DeviceState state_;
//     std::atomic<bool> initialized_;
//     std::mutex mutex_; // Для потокобезопасности

//     // === ВСПОМОГАТЕЛЬНЫЕ КОМПОНЕНТЫ ===

//     // 13. Таймеры и планировщик (для эмуляции времени)
//     std::unique_ptr<TimerScheduler> timer_scheduler_;

//     // 14. Генератор ошибок (для тестирования)
//     std::unique_ptr<ErrorInjector> error_injector_;

//     // 15. Логгер
//     std::unique_ptr<Logger> logger_;
// };

} // namespace vnic
