#pragma once

#include <expected>
#include "link/link.h"
#include "packet/packet_descriptor.h"
#include "stats/stats.h"
#include "utils/stateful.h"

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
 * @note Follows the @ref utils::Stateful automaton.
 * @ingroup group_queue
 */
class TxQueue final : public utils::Stateful<TxQueue> {
public:
    /**
     * @brief TX queue configuration (currently empty; reserved for future use).
     */
    struct Config {
        /// @brief Validate the configuration before it is applied.
        [[nodiscard]] bool isValid() const noexcept;
    };

    /**
     * @brief Queue one packet for transmission.
     * @param desc Descriptor whose @ref PacketDescriptor::buffer chain holds
     *             the packet data.
     * @return `ErrorCode::TxDescriptorNull` if the buffer is `nullptr`,
     *         `ErrorCode::TxSegmentLimitExceeded` if the chain exceeds
     *         `kMaxSegments`, `ErrorCode::TxLengthMismatch` on a bad chain,
     *         `ErrorCode::TxAllocationFailed` on OOM, `ErrorCode::TxLinkUnavailable`
     *         if the link is unset, or the link's error if it rejects the packet
     *         (all failure paths increment the drop counters).
     */
    [[nodiscard]] std::expected<void, ErrorCode> put(PacketDescriptor&& desc);

    /// @brief Queue configuration.
    const Config& getConfig() const;

    /// @brief Per-queue statistics.
    const stats::Stats& getStats() const;

    /// @brief Link this queue transmits into.
    ///
    /// Diagnostic accessor used to observe the queue's internal state in tests.
    /// @pre The queue has been configured. Calling this before
    ///      `configure()` is a programming error and is asserted in debug
    ///      builds.
    const Link& getLink() const noexcept;

private:
    /// @cond INTERNAL
    friend utils::Stateful<TxQueue>;
    /// @endcond

    std::expected<void, ErrorCode> configureImpl(const Config& config, Link& link);
    std::expected<void, ErrorCode> startImpl();
    std::expected<void, ErrorCode> stopImpl();

    Link* link_{};
    stats::Stats stats_;
    Config config_;
};

} // namespace vnic
