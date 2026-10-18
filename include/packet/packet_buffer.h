#pragma once

#include "constanst.h"
#include "parser/parser.h"

#include <span>
#include <ostream>

#include <cstdint>

/**
 * @file
 * @brief Segment of packet data used to build chains (Jumbo / TSO).
 */

namespace vnic {

/**
 * @brief One segment of a packet.
 *
 * The payload follows the struct in memory, so a buffer is a single
 * allocation of `sizeof(PacketBuffer) + length` bytes. Segments are chained via
 * @ref next to represent packets larger than one segment (Jumbo frames, TSO).
 *
 * @see PACKET_BUFFER_SIZE
 * @ingroup group_packet
 */
struct PacketBuffer {
    PacketBuffer* next{};   ///< Next segment carrying the rest of the packet, or `nullptr`.
    std::uint32_t length{}; ///< Payload length of @em this segment.

    /// @brief Payload of this segment as a mutable span.
    std::span<std::byte> getPacket() {
        return std::span<std::byte>{reinterpret_cast<std::byte*>(this) + sizeof(PacketBuffer),
                                    static_cast<std::size_t>(length)};
    }

    /// @brief Payload of this segment as a read-only span.
    std::span<const std::byte> getPacket() const {
        return std::span<const std::byte>{reinterpret_cast<const std::byte*>(this) + sizeof(PacketBuffer),
                                          static_cast<std::size_t>(length)};
    }

    /// @brief Parse and print the segment as a frame (debug helper).
    std::ostream& operator<<(std::ostream& os) const {
        Parser parser;
        if (parser.parse(getPacket())) {
            os << parser;
        } else {
            os << "Invalid packet metadata";
        }
        os << "\n";
        return os;
    }
};

/// @brief Stream operator for packet buffers.
inline std::ostream& operator<<(std::ostream& os, const PacketBuffer& pb) { return pb.operator<<(os); }

} // namespace vnic
