#pragma once

#include <expected>
#include "constants.h"
#include "utils/memory_pool.h"
#include "utils/fifo.h"
#include "utils/stateful.h"
#include "stats/stats.h"
#include "packet/packet_descriptor.h"

#include <array>
#include <algorithm>
#include <span>
#include <vector>

#include <cstring>
#include <cassert>
#include <cstdint>

/**
 * @file
 * @brief RX queue: the sink the poll loop distributes inbound frames to.
 */

namespace vnic {

/**
 * @defgroup group_queue Queues
 * @brief RX and TX queues, their configuration and per-queue statistics.
 * @{
 */

/**
 * @brief Receive queue holding descriptors ready to be fetched by the
 *        application.
 *
 * The queue keeps two FIFOs of descriptors: a pool of free descriptors and the
 * @c used FIFO of descriptors that reference received data. Inbound bytes are
 * copied into the queue's memory pool and split into @ref PacketBuffer
 * segments. The application calls `VNic::rx()` to drain @c used and
 * `VNic::freeRx()` to return descriptors and buffers to the queue.
 *
 * @note Follows the @ref utils::Stateful automaton; must be configured and
 *       started before use.
 */
class RxQueue final : public utils::Stateful<RxQueue> {
public:
    /**
     * @brief RX queue configuration.
     */
    struct Config {
        std::uint16_t queueId{0};  ///< Queue index within the port.
        std::uint32_t size{512};   ///< Number of descriptors (queue depth).
        std::uint32_t socketId{0}; ///< NUMA socket hint (reserved).

        /// @brief Validate the configuration before it is applied.
        [[nodiscard]] bool isValid() const noexcept;
    };

    /// @brief Construct a stopped queue.
    RxQueue() = default;
    /// @brief Queues are non-copyable.
    RxQueue(const RxQueue&) = delete;
    /// @brief Queues are non-copyable.
    RxQueue& operator=(const RxQueue&) = delete;
    /// @brief Move a stopped queue.
    RxQueue(RxQueue&&) noexcept = default;
    /// @brief Move-assign a stopped queue.
    RxQueue& operator=(RxQueue&&) noexcept = default;
    ~RxQueue();

    /**
     * @brief Start the queue.
     *
     * If the queue is still stopped, it is first configured with the last
     * applied configuration (default @ref Config for a fresh queue), so a queue
     * can be started without an explicit `configure()` call.
     */
    [[nodiscard]] std::expected<void, ErrorCode> start();

    /**
     * @brief Accept one frame.
     *
     * Copies @p packet into the memory pool (splitting it into segments),
     * attaches the buffers to @p desc and moves it to the @c used FIFO.
     *
     * @param desc   Descriptor to fill (@ref PacketDescriptor::buffer and
     *               @ref PacketDescriptor::totalLength are overwritten).
     * @param packet Raw frame bytes.
     * @return An empty `std::expected` on success; otherwise
     *         `ErrorCode::QueueZeroLengthPacket`, `ErrorCode::QueuePacketTooLarge`,
     *         `ErrorCode::QueueSegmentLimitExceeded`, `ErrorCode::QueueFreeListEmpty`
     *         (increments `overflows`), `ErrorCode::QueueUsedListFull` or the
     *         memory pool error.
     */
    [[nodiscard]] std::expected<void, ErrorCode> put(PacketDescriptor&& desc, std::span<const std::byte> packet);

    /**
     * @brief Drain up to `kMaxSegments` descriptors into @p descs.
     * @param descs Output; received descriptors are appended.
     * @return Number of descriptors appended (non-blocking).
     */
    [[nodiscard]] std::uint64_t take(std::vector<PacketDescriptor>& descs);

    /**
     * @brief Return descriptors (and the whole buffer chains) to the pool.
     * @param descs Descriptors previously obtained from `take()`.
     * @return `ErrorCode::QueueFreeListFull` if the free list cannot accept
     *         all descriptors; the chains are released regardless.
     */
    [[nodiscard]] std::expected<void, ErrorCode> free(std::span<const PacketDescriptor> descs);

    /// @brief Queue configuration.
    const Config& getConfig() const;

    /// @brief Per-queue statistics.
    const stats::Stats& getStats() const;

private:
    /// @cond INTERNAL
    friend utils::Stateful<RxQueue>;
    /// @endcond
    using MemPool = memory::Pool<kPacketBufferSize + sizeof(PacketBuffer)>;

    std::expected<void, ErrorCode> configureImpl(const Config& config);
    std::expected<void, ErrorCode> startImpl();
    std::expected<void, ErrorCode> stopImpl();

    void releaseChain(PacketBuffer* head) noexcept;

    Fifo<PacketDescriptor> free_;
    Fifo<PacketDescriptor> used_;
    MemPool memPool_;
    stats::Stats stats_;
    Config config_;

public:
    /// @name Introspection
    /// Diagnostic accessors used to observe the queue's internal state in tests.
    /// Not part of the steady-state API.
    /// @{

    /// @brief Pool of descriptors available for `put()` (`free_`).
    const Fifo<PacketDescriptor>& getFree() const noexcept { return free_; }

    /// @brief FIFO of descriptors holding received data (`used_`).
    const Fifo<PacketDescriptor>& getUsed() const noexcept { return used_; }

    /// @brief Underlying packet memory pool (`memPool_`).
    const MemPool& getMemPool() const noexcept { return memPool_; }

    /// @}
};

/// @}

} // namespace vnic
