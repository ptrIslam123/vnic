#pragma once

#include <cstdint>

/**
 * @file
 * @brief Compile-time constants shared across the library.
 */

namespace vnic {

/**
 * @defgroup group_constants Constants
 * @brief Global compile-time constants.
 * @{
 */

/// Maximum number of segments in a buffer chain, and the maximum number of
/// descriptors drained by a single `RxQueue::take()`.
inline constexpr auto kMaxSegments{512};

/// Size of the payload area of one @ref PacketBuffer segment.
inline constexpr auto kPacketBufferSize{2048};

/// Minimum frame size in this model. The @ref Link carries no FCS, so the
/// threshold is 60 bytes (46 payload + 14 Ethernet header) rather than 64.
inline constexpr std::size_t kMinFrameSize{60};

/// @}

} // namespace vnic
