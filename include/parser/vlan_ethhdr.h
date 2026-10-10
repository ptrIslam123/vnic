#pragma once

#include <linux/if_ether.h>

namespace vlan {

struct vlan_ethhdr {
    unsigned char h_dest[ETH_ALEN];     /* destination eth addr */
    unsigned char h_source[ETH_ALEN];   /* source ether addr    */
    __be16        h_vlan_proto;         /* always 0x8100        */
    __be16        h_vlan_TCI;           /* priority and VLAN ID */
    __be16        h_vlan_encapsulated_proto; /* packet type ID (or len) */
};

} // namespace vlan
