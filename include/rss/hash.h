#pragma once

#include "packet/five_tuple.h"

#include <span>
#include <cstdint>

namespace vnic::rss {

enum class HashFunc : std::uint32_t {
    NONE = 0,
    TOEPLITZ = 1,
    SIMPLE_XOR = 2,
    CRC32 = 4,
};

enum class Protocol : std::uint32_t {
    NONE = 0,
    IPV4 = 1 << 0,         // Хэшировать по IPv4 адресам
    IPV6 = 1 << 1,         // Хэшировать по IPv6 адресам
    TCP = 1 << 2,          // Учитывать TCP порты
    UDP = 1 << 3,          // Учитывать UDP порты
    SCTP = 1 << 4,         // Учитывать SCTP порты
    ALL = IPV4 | IPV6 | TCP | UDP | SCTP
};

enum class SymmetryMode : std::uint8_t {
    NONE  = 0,
    ASYMMETRIC,   // A→B и B→A дают РАЗНЫЕ хэши
    SYMMETRIC,    // A→B и B→A дают ОДИНАКОВЫЕ хэши
};

namespace soft {

std::uint32_t CalcHash(
    FiveTuple tuple,
    std::span<const std::uint8_t> key,
    HashFunc hf, Protocol protocol, SymmetryMode symmetry
);

} // namespace vnic::rss::soft

} // namespace vnic::rss
