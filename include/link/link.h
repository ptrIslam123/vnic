#pragma once

#include "utils/fifo.h"
#include "utils/allocator.h"

#include <string>
#include <span>
#include <thread>
#include <chrono>

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
 * On the RX side, `VNic::poll()` reads inbound frames with `takeRx()`. While the
 * link is down (`isUp() == false`) the poll loop does not read.
 *
 * @note Version 0.1.0 implements only this in-memory link; there is no
 *       kernel/netlink backend.
 */
class Link final {
public:
    /**
     * @brief Link parameters.
     */
    struct Config {
        /// Duplex mode.
        enum class Duplex : std::uint8_t {
            Half, ///< Half duplex.
            Full, ///< Full duplex.
        };

        std::uint32_t speedMbps{1000}; ///< Nominal link speed in Mbit/s.
        Duplex duplex{Duplex::Full};   ///< Duplex mode.
    };

    /// @brief Apply link parameters.
    bool configure(const Config& config);

    /// @brief Bring the link up (enables RX polling).
    bool up();

    /// @brief Bring the link down (pauses RX polling).
    bool down();

    /// @brief Whether the link is currently up.
    bool isUp() const noexcept;

    /// @brief Enqueue an inbound frame (copies @p packet).
    bool putRx(std::span<std::byte> packet);

    /**
     * @brief Dequeue an inbound frame.
     * @param packet  Output; the frame bytes are appended.
     * @param timeout Maximum time to wait.
     * @return Number of bytes appended, or `0` on timeout.
     */
    std::size_t takeRx(std::vector<std::byte>& packet, std::chrono::milliseconds timeout);

    /// @brief Enqueue an outbound frame (copies @p packet).
    bool putTx(std::span<std::byte> packet);

    /**
     * @brief Dequeue an outbound frame.
     * @param packet  Output; the frame bytes are appended.
     * @param timeout Maximum time to wait.
     * @return Number of bytes appended, or `0` on timeout.
     */
    std::size_t takeTx(std::vector<std::byte>& packet, std::chrono::milliseconds timeout);

private:
    using Queue     = Fifo<std::span<std::byte>>;
    using Allocator = memory::Allocator;

    static bool put(std::span<std::byte> packet, Queue& queue, Allocator& allocator);
    static std::size_t take(std::vector<std::byte>& packet, Queue& queue, Allocator& allocator,
                            std::chrono::milliseconds timeout);

    Queue rxQueue_;
    Queue txQueue_;
    Allocator rxAllocator_;
    Allocator txAllocator_;
    Config config_;
    bool up_{false};
};

/// @}

} // namespace vnic
