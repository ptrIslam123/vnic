#pragma once

#include "packet/packet.h"

#include <span>
#include <cstdint>

namespace vnic::rss {

enum class HashFunc : std::uint32_t {
    NONE = 0,
    TOEPLITZ = 1,          // Классический Toeplitz хэш (самый распространенный)
    SIMPLE_XOR = 2,        // Простой XOR (для тестирования)
    CRC32 = 4,             // CRC32 (некоторые NIC поддерживают)
    SYNCE = 8,             // Synchronous Ethernet
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

namespace soft {

std::uint32_t calc_hash(const vnic::Packet& packet, std::span<const std::uint8_t> key, HashFunc hf, Protocol protocol);

} // namespace vnic::rss::soft

} // namespace vnic::rss
