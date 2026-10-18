#pragma once

#include "constanst.h"
#include "utils/memory_pool.h"
#include "utils/fifo.h"
#include "utils/state_ful.h"
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
 * @note Follows the @ref utils::Stateful automate; must be configured and
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
    };

    /**
     * @brief Accept one frame.
     *
     * Copies @p packet into the memory pool (splitting it into segments),
     * attaches the buffers to @p desc and moves it to the @c used FIFO.
     *
     * @param desc   Descriptor to fill (@ref PacketDescriptor::buffer and
     *               @ref PacketDescriptor::totalLength are overwritten).
     * @param packet Raw frame bytes.
     * @return `true` on success; `false` on empty input, a full queue
     *         (increments `overflows`) or pool exhaustion.
     */
    bool put(PacketDescriptor&& desc, std::span<const std::byte> packet);

    /**
     * @brief Drain up to @ref MAX_SEGMENTS descriptors into @p descs.
     * @param descs Output; received descriptors are appended.
     * @return Number of descriptors appended (non-blocking).
     */
    std::uint64_t take(std::vector<PacketDescriptor>& descs);

    /**
     * @brief Return descriptors (and the whole buffer chains) to the pool.
     * @param descs Descriptors previously obtained from `take()`.
     * @return `true`.
     */
    bool free(std::span<const PacketDescriptor> descs);

    /// @brief Queue configuration.
    const Config& getConfig() const;

    /// @brief Per-queue statistics.
    const stats::Stats& getStats() const;

private:
    /// @cond INTERNAL
    friend utils::Stateful<RxQueue>;
    /// @endcond
    using MemPool = memory::Pool<PACKET_BUFFER_SIZE + sizeof(PacketBuffer)>;

    bool configureImpl(const Config& config);
    bool startImpl();
    bool stopImpl();

    Fifo<PacketDescriptor> free_;
    Fifo<PacketDescriptor> used_;
    MemPool memPool_;
    stats::Stats stats_;
    Config config_;
};

/// @}

} // namespace vnic
