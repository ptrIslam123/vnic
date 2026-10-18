#pragma once

#include "link/link.h"
#include "stats/stats.h"
#include "utils/state_ful.h"
#include "packet/packet_descriptor.h"

#include <span>

/**
 * @file
 * @brief TX queue: the path a transmitted packet takes toward the link.
 */

namespace vnic {

/**
 * @brief Transmit queue.
 *
 * `put()` gathers all segments of the descriptor's buffer chain into one
 * contiguous buffer and copies it into the associated @ref Link TX pipe, where
 * it can be drained with `Link::takeTx()`.
 *
 * @note Follows the @ref utils::Stateful automate.
 * @ingroup group_queue
 */
class TxQueue final : public utils::Stateful<TxQueue> {
public:
    /**
     * @brief TX queue configuration (currently empty; reserved for future use).
     */
    struct Config {};

    /**
     * @brief Queue one packet for transmission.
     * @param desc Descriptor whose @ref PacketDescriptor::buffer chain holds
     *             the packet data.
     * @return `true` on success; `false` if the buffer is `nullptr`, the chain
     *         exceeds @ref MAX_SEGMENTS, the link is unset, or the link rejects
     *         the packet (increments the drop counters).
     */
    bool put(PacketDescriptor&& desc);

    /// @brief Queue configuration.
    const Config& getConfig() const;

    /// @brief Per-queue statistics.
    const stats::Stats& getStats() const;

private:
    /// @cond INTERNAL
    friend utils::Stateful<TxQueue>;
    /// @endcond

    bool configureImpl(const Config& config, Link& link);
    bool startImpl();
    bool stopImpl();

    Link* link_{};
    stats::Stats stats_;
    Config config_;
};

} // namespace vnic
