#pragma once

#include "packet/packet_buffer.h"

#include <ostream>

#include <cstdint>

/**
 * @file
 * @brief Packet descriptor — the RX/TX unit of work.
 */

namespace vnic {

/**
 * @defgroup group_packet Packet representation
 * @brief Descriptors and buffer chains describing packets.
 * @{
 */

/**
 * @brief Metadata describing one packet plus a pointer to its data.
 *
 * A descriptor does not own the data: @ref buffer points into the RX queue's
 * memory pool (on receive) or into a caller-provided chain (on transmit). The
 * RX path fills @ref hash, @ref queueId, @ref vlanId and @ref timestamp; the
 * TX path expects @ref buffer and @ref totalLength to be set by the caller.
 */
struct PacketDescriptor {
    std::uint64_t timestamp{};   ///< Receive/transmit time (monotonic nanoseconds).
    std::uint32_t hash{};        ///< RSS hash (0 for non-IP frames).
    std::uint16_t queueId{};     ///< Queue index the packet was assigned to.
    std::uint32_t offloads{};    ///< Offload flags (reserved for future use).
    std::uint16_t vlanId{};      ///< VLAN tag extracted from the frame.
    std::uint32_t totalLength{}; ///< Total length across all segments.
    PacketBuffer* buffer{};      ///< Head of the buffer chain (may be `nullptr`).

    /// @brief Print the descriptor to an output stream (debug helper).
    std::ostream& operator<<(std::ostream& os) const {
        os << "[Packet]"
           << " queue_id=" << queueId << " hash=0x" << std::hex << hash << std::dec << " len=" << totalLength
           << " vlan=" << vlanId << " offloads=0x" << std::hex << offloads << std::dec << " timestamp=" << timestamp
           << " buffer=" << static_cast<const void*>(buffer);
        return os;
    }
};

/// @brief Stream operator for @ref PacketDescriptor.
inline std::ostream& operator<<(std::ostream& os, const PacketDescriptor& pd) { return pd.operator<<(os); }

/// @}

} // namespace vnic
