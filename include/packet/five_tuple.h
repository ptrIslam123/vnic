#pragma once

#include <cstdint>

#include <linux/types.h>

namespace vnic {

// Пятерка адресов потока, по которой считаются RSS-хэши.
// Поля хранятся в сетевом порядке байт (__be32/__be16), как в DPDK.
struct FiveTuple {
    __be32 sadr;
    __be32 dadr;
    __be16 sport;
    __be16 dport;
    std::uint8_t iproto;
};

} // namespace vnic