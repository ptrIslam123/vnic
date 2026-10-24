#pragma once

#include <cstdint>

/**
 * @file
 * @brief Monotonic clock helper.
 */

namespace vnic::clock::monotonic {

/**
 * @defgroup group_clock Clock
 * @brief Time source used for packet timestamps.
 * @{
 */

/**
 * @brief Current monotonic time in nanoseconds.
 *
 * Used to stamp @ref PacketDescriptor::timestamp. Monotonic, so it is not
 * affected by wall-clock adjustments.
 */
std::uint64_t Now();

/// @}

} // namespace vnic::clock::monotonic
