#pragma once

#include <expected>
#include "link/link.h"
#include "queue/rx_queue.h"
#include "queue/tx_queue.h"
#include "rss/hash.h"
#include "utils/rcu.h"
#include "utils/stateful.h"
#include "validate/validate.h"

#include <array>
#include <cstddef>
#include <span>
#include <thread>
#include <vector>

#include <cstdint>

/**
 * @file
 * @brief The VNic port — the top-level object that ties queues, RSS and the
 * link together and runs the RX poll loop.
 */

namespace vnic {

class Parser;

/**
 * @defgroup group_vnic Port (VNic)
 * @brief Top-level NIC port: configuration, RX/TX queues, RSS/RETA and the
 * link.
 * @{
 */

/**
 * @brief A software emulation of a network interface card (NIC) port.
 *
 * A `VNic` owns:
 *  - a @ref Link — the in-memory pipe used to inject incoming frames and drain
 *    outgoing ones;
 *  - a set of @ref RxQueue "RX queues" and @ref TxQueue "TX queues";
 *  - an RSS redirection table (@ref Config::Rss::Reta) that maps packet hashes
 *    to RX queues;
 *  - aggregate @ref stats::Stats.
 *
 * **Lifecycle.** The class is non-copyable/non-movable and follows the
 * @ref utils::Stateful automaton: call `configure()` first, then `start()`
 * (which launches the internal RX poll thread), and finally `stop()`.
 *
 * **Thread safety.** `start()`/`stop()` manage an internal `std::jthread`. The
 * RX poll loop is the single reader of inbound frames. The packet hashing /
 * queue lookup on the hot path reads the RETA through @ref utils::Rcu, so RETA
 * may be replaced at runtime with `updateReta()`.
 *
 * @note This is version 0.1.x: buffer pools are `malloc`-backed stubs, there
 * are no offloads, and the link is purely in-memory (no kernel/netlink).
 */
class VNic final : public utils::Stateful<VNic> {
public:
    VNic()                       = default;
    VNic(const VNic&)            = delete;
    VNic(VNic&&)                 = delete;
    VNic& operator=(const VNic&) = delete;
    VNic& operator=(VNic&&)      = delete;
    ~VNic();

    /**
     * @brief Port configuration passed to `configure(const Config&)`.
     */
    struct Config {
        bool promiscuousEnabled{false}; ///< Disable the destination-MAC filter.
        std::uint32_t mtu{1500};        ///< Maximum L3 payload size in bytes.
        std::uint16_t rxQueueCount{1};  ///< Number of RX queues.
        std::uint16_t txQueueCount{0};  ///< Number of TX queues.

        /**
         * @brief Receive Side Scaling configuration.
         */
        struct Rss {
            /// Redirection table: hash index -> RX queue id.
            using Reta = std::vector<std::uint16_t>;
            /// RSS key bytes (e.g. 40 bytes for Toeplitz).
            using Key = std::vector<std::uint8_t>;

            bool enabled{false};          ///< Enable RSS.
            rss::HashFunc hf{};           ///< Hash function to use.
            rss::Protocol protocol{};     ///< Fields to include in the hash input.
            rss::SymmetryMode symmetry{}; ///< Whether A->B and B->A hash equally.
            Key key{};                    ///< Hash key (at least 40 bytes when enabled).
            Reta reta{};                  ///< Explicit RETA; empty = round-robin default.

            /// @brief Validate the RSS configuration and return the reason.
            [[nodiscard]] ErrorCode validate() const noexcept;

            /// @brief Validate the RSS configuration.
            [[nodiscard]] bool isValid() const noexcept;
        } rss; ///< RSS configuration.

        std::string macAddr{};                      ///< MAC address `"aa:bb:cc:dd:ee:ff"` (empty allowed).
        std::vector<std::string> ipAddrs{};         ///< Port IP addresses (not yet used on the RX path).
        std::vector<std::uint16_t> vlanAllowList{}; ///< Allowed VLAN IDs; empty = VLAN filter disabled.
        Link::Config link{};                        ///< Link pipe depths and slot sizes.

        /// @brief Validate the whole port configuration and return the reason.
        [[nodiscard]] ErrorCode validate() const noexcept;

        /// @brief Validate the whole port configuration.
        [[nodiscard]] bool isValid() const noexcept;
    };

    /// @name Link
    /// @{

    /// @brief Read-only access to the link.
    const Link& getLink() const;
    /// @brief Mutable access to the link (e.g. `up()` / `putRx()`).
    Link& getLink();
    /// @}

    /// @name RETA (runtime reconfiguration)
    /// @{
    /**
     * @brief Replace the RSS redirection table.
     * @param reta New table. Must be non-empty, its size a power of two, and
     *             every entry `< Config::rxQueueCount`.
     * @return `ErrorCode::RssRetaEmpty`, `ErrorCode::RssRetaSizeInvalid` or
     *         `ErrorCode::RssRetaQueueOutOfRange` if validation fails.
     * @note Thread-safe; the RX poll loop observes the new table atomically.
     */
    [[nodiscard]] std::expected<void, ErrorCode> updateReta(Config::Rss::Reta&& reta);

    /**
     * @brief Replace the RSS redirection table (copy overload).
     * @param reta New table (same constraints as the rvalue overload).
     * @return Same errors as the rvalue overload.
     */
    [[nodiscard]] std::expected<void, ErrorCode> updateReta(const Config::Rss::Reta& reta);
    /// @}

    /// @name Data path
    /// @{
    /**
     * @brief Transmit a burst of packets on a TX queue.
     *
     * Mirrors the DPDK burst semantics: transmission stops at the first
     * descriptor that cannot be queued.
     *
     * @param queueId Target TX queue index.
     * @param descs   Descriptors to send. Each descriptor must reference a
     *                @ref PacketBuffer chain.
     * @return Number of descriptors actually transmitted. A partial burst is a
     *         success; if nothing was transmitted because of an error (including
     *         an invalid @p queueId), the error is returned.
     */
    [[nodiscard]] std::expected<std::size_t, ErrorCode> tx(std::uint16_t queueId, std::span<PacketDescriptor> descs);

    /**
     * @brief Receive up to `kMaxSegments` descriptors from an RX queue.
     *
     * Non-blocking: appends whatever is currently available to @p descs.
     *
     * @param queueId Source RX queue index.
     * @param descs   Output vector; received descriptors are appended.
     * @return Number of descriptors appended; `ErrorCode::QueueInvalidId` if
     *         @p queueId is out of range.
     * @warning Return the descriptors with `freeRx()`; their buffers belong to
     *          the queue's memory pool.
     */
    [[nodiscard]] std::expected<std::size_t, ErrorCode> rx(std::uint16_t queueId, std::vector<PacketDescriptor>& descs);

    /**
     * @brief Return descriptors previously obtained via `rx()` to the queue.
     * @param queueId Queue the descriptors came from.
     * @param descs   Descriptors to release (all `next` segments are freed).
     * @return `ErrorCode::QueueInvalidId` if @p queueId is out of range, or the
     *         queue's error if the descriptors cannot be released.
     */
    [[nodiscard]] std::expected<void, ErrorCode> freeRx(std::uint16_t queueId, std::span<const PacketDescriptor> descs);
    /// @}

    /// @name Statistics
    /// @{

    /// @brief Reset the aggregate port counters to zero.
    void resetStats();
    /// @}

    /// @name Introspection
    /// @{
    /**
     * @brief Get an RX queue by index.
     * @return Pointer to the queue, or `nullptr` if @p queueId is out of range.
     */
    const RxQueue* getRxQueue(std::uint16_t queueId) const;

    /**
     * @brief Get a TX queue by index.
     * @return Pointer to the queue, or `nullptr` if @p queueId is out of range.
     */
    const TxQueue* getTxQueue(std::uint16_t queueId) const;

    /// @brief Current port configuration.
    const Config& getConfig() const;

    /// @brief Aggregate port statistics.
    const stats::Stats& getStats() const;

    /// @brief Copy of the current RETA (control-plane use).
    Config::Rss::Reta getReta() const;

    /// @}

private:
    /// @cond INTERNAL
    friend utils::Stateful<VNic>;
    /// @endcond

    std::expected<void, ErrorCode> startImpl();
    std::expected<void, ErrorCode> stopImpl();

    std::expected<void, ErrorCode> configureImpl(const Config& config);
    std::expected<void, ErrorCode> configureImpl(std::uint16_t queueId, const RxQueue::Config& config);
    std::expected<void, ErrorCode> configureImpl(std::uint16_t queueId, const TxQueue::Config& config);

    std::expected<void, ErrorCode> initReta(Config::Rss::Reta& reta);
    std::expected<void, ErrorCode> configureRss();
    std::expected<void, ErrorCode> configureQueues();
    std::expected<void, ErrorCode> configureLink();

    std::expected<void, ErrorCode> validate(std::span<const std::byte> packet, const Parser& parser);

    std::expected<void, ErrorCode> distribute(const Parser& parser, std::span<const std::byte> payload);
    void poll(std::stop_token stopToken);

    std::expected<void, ErrorCode> startQueues();
    std::expected<void, ErrorCode> stopQueues();

    Link link_;
    utils::Rcu<Config::Rss::Reta> reta_;
    std::vector<RxQueue> rxQueues_;
    std::vector<TxQueue> txQueues_;
    stats::Stats stats_;

    std::vector<RxQueue::Config> rxQueueConfigs_;
    std::vector<TxQueue::Config> txQueueConfigs_;
    Config config_;
    std::array<std::uint8_t, 6> macAddr_{};

    std::jthread thread_;
};

/// @}

} // namespace vnic
