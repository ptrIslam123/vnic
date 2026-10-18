#pragma once

#include "parser/parser.h"

#include <cstdint>

#include <linux/types.h>
#include <netinet/in.h>

/**
 * @file
 * @brief The flow 5-tuple used as RSS hash input.
 */

namespace vnic {

/**
 * @brief 5-tuple identifying a network flow, used as RSS hash input.
 *
 * Address and port fields are kept in network byte order (`__be32` / `__be16`),
 * matching DPDK. The @ref version field selects which address family is valid:
 * @ref kIpv4 means @ref saddr4 / @ref daddr4 are set, @ref kIpv6 means
 * @ref saddr6 / @ref daddr6 are set.
 *
 * @ingroup group_rss
 */
struct FiveTuple {
    static constexpr std::uint8_t kIpv4{4}; ///< IPv4 marker for @ref version.
    static constexpr std::uint8_t kIpv6{6}; ///< IPv6 marker for @ref version.

    std::uint8_t version{}; ///< Address family: @ref kIpv4 or @ref kIpv6.
    __be32 saddr4{};        ///< IPv4 source address (network order).
    __be32 daddr4{};        ///< IPv4 destination address (network order).
    in6_addr saddr6{};      ///< IPv6 source address.
    in6_addr daddr6{};      ///< IPv6 destination address.
    __be16 sport{};         ///< L4 source port (network order).
    __be16 dport{};         ///< L4 destination port (network order).
    std::uint8_t iproto{};  ///< L4 protocol number.
};

/**
 * @brief Build the RSS 5-tuple for a parsed frame.
 * @param parser Parser already positioned on an IPv4 or IPv6 frame.
 * @return The 5-tuple. For non-IPv4/IPv6 frames (e.g. ARP) an all-zero tuple is
 *         returned; RSS does not apply to such frames.
 */
FiveTuple Get5Tuple(const Parser& parser);

} // namespace vnic
