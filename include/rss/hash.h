#pragma once

#include "packet/five_tuple.h"

#include <span>
#include <cstdint>

/**
 * @file
 * @brief Receive Side Scaling hash functions.
 */

namespace vnic::rss {

/**
 * @defgroup group_rss Receive Side Scaling
 * @brief Hash functions, the flow 5-tuple and the redirection table (RETA).
 * @{
 */

/// Hash function selected for RSS.
enum class HashFunc : std::uint32_t {
    NONE       = 0, ///< No hashing (all hashes are 0).
    TOEPLITZ   = 1, ///< Microsoft/Toeplitz hash (needs a >= 40-byte key).
    SIMPLE_XOR = 2, ///< Simple XOR hash (educational).
    CRC32      = 4, ///< CRC32-based hash.
};

/// Bit flags selecting which header fields feed the hash input.
enum class Protocol : std::uint32_t {
    NONE = 0,
    IPV4 = 1 << 0,                         ///< Include IPv4 addresses.
    IPV6 = 1 << 1,                         ///< Include IPv6 addresses.
    TCP  = 1 << 2,                         ///< Include TCP ports.
    UDP  = 1 << 3,                         ///< Include UDP ports.
    SCTP = 1 << 4,                         ///< Include SCTP ports.
    ALL  = IPV4 | IPV6 | TCP | UDP | SCTP, ///< All fields.
};

/// Symmetry of the hash with respect to flow direction.
enum class SymmetryMode : std::uint8_t {
    NONE = 0,
    ASYMMETRIC, ///< A->B and B->A produce different hashes.
    SYMMETRIC,  ///< A->B and B->A produce the same hash.
};

namespace soft {

/**
 * @brief Compute an RSS hash for a flow in software.
 *
 * @param tuple     Flow 5-tuple (see @ref vnic::Get5Tuple).
 * @param key       RSS key. Toeplitz requires at least 40 bytes; shorter keys
 *                  yield a hash of 0.
 * @param hf        Hash function to apply.
 * @param protocol  Which fields of @p tuple participate in the hash input.
 * @param symmetry  Whether to canonicalize the tuple for direction symmetry.
 * @return The 32-bit hash value.
 */
std::uint32_t CalcHash(FiveTuple tuple, std::span<const std::uint8_t> key, HashFunc hf, Protocol protocol,
                       SymmetryMode symmetry);

} // namespace soft

/// @}

} // namespace vnic::rss
