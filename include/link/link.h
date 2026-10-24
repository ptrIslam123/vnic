#pragma once

#include <expected>
#include "error_codes.h"
#include "utils/allocator.h"
#include "utils/fifo.h"

#include <atomic>
#include <chrono>
#include <span>
#include <vector>

#include <cstdint>
#include <cstring>

/**
 * @file
 * @brief The link — an in-memory pipe between the application and the port.
 */

namespace vnic {

/**
 * @defgroup group_link Link
 * @brief In-memory transport used to feed and drain the port.
 * @{
 */

/**
 * @brief In-memory link (pipe).
 *
 * Conceptually a pair of pipes: one for inbound frames (application -> port)
 * and one for outbound frames (port -> application). Bytes are copied into the
 * link on insertion, so the caller may reuse or free its buffer immediately.
 *
 * On the RX side, `VNic::poll()` reads inbound frames with `takeRx()`. While
 * the link is down (`isUp() == false`) the poll loop does not read.
 *
 * @note Version 0.1.x implements only this in-memory link; there is no
 *       kernel/netlink backend.
 */
class Link final {
public:
    /**
     * @brief Link parameters: capacities of the two in-memory pipes.
     *
     * The link does not emulate throughput, speed or duplex. It only needs to
     * know how many frames each pipe may hold and how much memory to reserve
     * for one frame slot. Slot sizes are reservation hints: a frame larger than
     * a slot is still accepted (the 0.1.x allocator is `malloc`-backed).
     */
    struct Config {
        std::uint32_t rxQueueDepth{256}; ///< RX pipe depth (frames).
        std::uint32_t txQueueDepth{256}; ///< TX pipe depth (frames).
        std::uint32_t rxFrameSize{4096}; ///< Bytes reserved per RX slot (hint).
        std::uint32_t txFrameSize{4096}; ///< Bytes reserved per TX slot (hint).

        /// @brief Bytes reserved for all RX pipe slots.
        [[nodiscard]] constexpr std::uint64_t rxReservedBytes() const noexcept {
            return static_cast<std::uint64_t>(rxQueueDepth) * rxFrameSize;
        }

        /// @brief Bytes reserved for all TX pipe slots.
        [[nodiscard]] constexpr std::uint64_t txReservedBytes() const noexcept {
            return static_cast<std::uint64_t>(txQueueDepth) * txFrameSize;
        }

        /// @brief Validate the configuration before it is applied.
        [[nodiscard]] ErrorCode validate() const noexcept {
            if (rxQueueDepth == 0) {
                return ErrorCode::ConfigLinkRxDepthInvalid;
            }
            if (txQueueDepth == 0) {
                return ErrorCode::ConfigLinkTxDepthInvalid;
            }
            if (rxFrameSize == 0) {
                return ErrorCode::ConfigLinkRxFrameSizeInvalid;
            }
            if (txFrameSize == 0) {
                return ErrorCode::ConfigLinkTxFrameSizeInvalid;
            }
            return ErrorCode::None;
        }

        /// @brief Whether the configuration is valid.
        [[nodiscard]] bool isValid() const noexcept { return validate() == ErrorCode::None; }
    };

    Link()                       = default;
    Link(const Link&)            = delete;
    Link& operator=(const Link&) = delete;
    Link(Link&&)                 = delete;
    Link& operator=(Link&&)      = delete;
    ~Link();

    /// @brief Apply link parameters.
    [[nodiscard]] std::expected<void, ErrorCode> configure(const Config& config);

    /// @brief Link parameters (also used to observe configuration in tests).
    const Config& getConfig() const noexcept { return config_; }

    /// @brief Bring the link up (enables RX polling).
    [[nodiscard]] std::expected<void, ErrorCode> up();

    /// @brief Bring the link down (pauses RX polling).
    [[nodiscard]] std::expected<void, ErrorCode> down();

    /// @brief Whether the link is currently up.
    [[nodiscard]] bool isUp() const noexcept;

    /// @brief Enqueue an inbound frame (copies @p packet).
    [[nodiscard]] std::expected<void, ErrorCode> putRx(std::span<std::byte> packet);

    /**
     * @brief Dequeue an inbound frame.
     * @param packet  Output; the frame bytes are appended.
     * @param timeout Maximum time to wait.
     * @return Number of bytes appended; `ErrorCode::Timeout` on timeout.
     */
    [[nodiscard]] std::expected<std::size_t, ErrorCode> takeRx(std::vector<std::byte>& packet,
                                                               std::chrono::milliseconds timeout);

    /// @brief Enqueue an outbound frame (copies @p packet).
    [[nodiscard]] std::expected<void, ErrorCode> putTx(std::span<std::byte> packet);

    /**
     * @brief Dequeue an outbound frame.
     * @param packet  Output; the frame bytes are appended.
     * @param timeout Maximum time to wait.
     * @return Number of bytes appended; `ErrorCode::Timeout` on timeout.
     */
    [[nodiscard]] std::expected<std::size_t, ErrorCode> takeTx(std::vector<std::byte>& packet,
                                                               std::chrono::milliseconds timeout);

private:
    using Queue     = Fifo<std::span<std::byte>>;
    using Allocator = memory::Allocator;

    std::expected<void, ErrorCode> put(std::span<std::byte> packet, Queue& queue, Allocator& allocator,
                                       ErrorCode fullError);
    static std::expected<std::size_t, ErrorCode> take(std::vector<std::byte>& packet, Queue& queue,
                                                      Allocator& allocator, std::chrono::milliseconds timeout);
    static void drain(Queue& queue, Allocator& allocator) noexcept;

    Queue rxQueue_;
    Queue txQueue_;
    Allocator rxAllocator_;
    Allocator txAllocator_;
    Config config_;
    std::atomic<bool> up_{false};
};

/// @}

} // namespace vnic
