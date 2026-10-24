#pragma once

#include <linux/if_ether.h>

/**
 * @file
 * @brief 802.1Q VLAN-tagged Ethernet header.
 */

namespace vlan {

/**
 * @brief An Ethernet header extended with a single 802.1Q VLAN tag.
 * @ingroup group_parser
 */
struct vlan_ethhdr {
    unsigned char h_dest[ETH_ALEN];   ///< Destination MAC address.
    unsigned char h_source[ETH_ALEN]; ///< Source MAC address.
    __be16 h_vlan_proto;              ///< TPID, always 0x8100.
    __be16 h_vlan_TCI;                ///< Priority (3 bits) and VLAN ID (12 bits).
    __be16 h_vlan_encapsulated_proto; ///< Encapsulated EtherType (or length).
};

} // namespace vlan
