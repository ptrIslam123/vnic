#include "rss/hash.h"

#include <cstring>
#include <cassert>
#include <algorithm>

#include <netinet/in.h>

#include <iostream>

namespace vnic::rss::soft {

namespace {

// ============================================================
//  TOEPLITZ (320-bit key, 10 x 32-bit words)
// ============================================================
void key320_turnkey(std::uint8_t* key, int len) {
    for (int i = 0; i < (len >> 1); ++i) {
        const int r = len - i - 1;
        std::swap(key[i], key[r]);
    }
}

void key320_rotate(std::uint32_t* K) {
    const std::uint32_t tmp = K[9];

    K[9] = (K[8] >> 31) | (K[9] << 1);
    K[8] = (K[7] >> 31) | (K[8] << 1);
    K[7] = (K[6] >> 31) | (K[7] << 1);
    K[6] = (K[5] >> 31) | (K[6] << 1);
    K[5] = (K[4] >> 31) | (K[5] << 1);
    K[4] = (K[3] >> 31) | (K[4] << 1);
    K[3] = (K[2] >> 31) | (K[3] << 1);
    K[2] = (K[1] >> 31) | (K[2] << 1);
    K[1] = (K[0] >> 31) | (K[1] << 1);
    K[0] = (tmp  >> 31) | (K[0] << 1);
}

std::uint32_t key320_hash_calc(
    const std::uint32_t* key,
    std::uint32_t hash,
    const std::uint8_t* data,
    int bits
    ) {
    const auto* keyBytes = reinterpret_cast<const std::uint8_t*>(key);
    std::size_t keyOffset = 0;

    for (int i = 0; i < bits; ++i) {
        const bool bit = (data[i >> 3] >> (7 - (i & 7))) & 1;

        if (bit) {
            hash ^= *reinterpret_cast<const std::uint32_t*>(keyBytes + keyOffset);
        }

        keyOffset += 4;
        if (keyOffset + 4 > 40) {
            keyOffset = 0;   // циклический ключ
        }
    }

    return hash;
}

// ============================================================
//  ПРОСТОЙ XOR
// ============================================================
std::uint32_t simple_xor_calc(
    const FiveTuple& tuple,
    std::span<const std::uint8_t> key,
    Protocol protocol
) {
    std::uint32_t hash = 0;
    const auto* data = reinterpret_cast<const std::uint8_t*>(&tuple);
    constexpr std::size_t dataLen = sizeof(FiveTuple);

    for (std::size_t i = 0; i < dataLen; ++i) {
        hash ^= static_cast<std::uint32_t>(data[i]) << ((i % 4) * 8);
    }
    return hash;
}

// ============================================================
//  СИММЕТРИЧНАЯ ПРЕДОБРАБОТКА
// ============================================================
// Сортирует IP и порты так, чтобы A→B и B→A давали ОДИН вход.
// ============================================================
void MakeSymmetric(FiveTuple& tuple) {
    const auto srcIp = ntohl(tuple.sadr);
    const auto dstIp = ntohl(tuple.dadr);
    if (srcIp > dstIp) {
        std::swap(tuple.sadr, tuple.dadr);
        std::swap(tuple.sport, tuple.dport);
    } else if (srcIp == dstIp) {
        const auto srcPort = ntohs(tuple.sport);
        const auto dstPort = ntohs(tuple.dport);
        if (srcPort > dstPort) {
            std::swap(tuple.sport, tuple.dport);
        }
    }
}

// ============================================================
//  ДЛИНА ХЭШИРУЕМОЙ ПОСЛЕДОВАТЕЛЬНОСТИ
// ============================================================
std::size_t hashDataLength(Protocol protocol) {
    const auto proto = static_cast<std::uint32_t>(protocol);

    const bool hasIp = (proto & (
                            static_cast<std::uint32_t>(Protocol::IPV4) |
                            static_cast<std::uint32_t>(Protocol::IPV6)
                            )) != 0;

    const bool hasL4 = (proto & (
                            static_cast<std::uint32_t>(Protocol::TCP) |
                            static_cast<std::uint32_t>(Protocol::UDP) |
                            static_cast<std::uint32_t>(Protocol::SCTP)
                            )) != 0;

    if (hasIp && hasL4) return 13;   // 13 байт
    if (hasIp)          return 8;                    // srcIP + dstIP
    return 0;
}

} // namespace

// ============================================================
//  CALC HASH
// ============================================================
std::uint32_t CalcHash(
    FiveTuple tuple,
    std::span<const std::uint8_t> key,
    HashFunc hf,
    Protocol protocol,
    SymmetryMode symmetry
) {
    assert(key.size() >= 40 && "RSS key is too short, expected at least 40 bytes");
    if (symmetry == SymmetryMode::SYMMETRIC) {
        MakeSymmetric(tuple);
    }

    // 2. Выбор алгоритма
    switch (hf) {
        case HashFunc::TOEPLITZ: {
            const auto dataLen = hashDataLength(protocol);
            if (dataLen == 0) return 0;

            const auto bits = static_cast<int>(dataLen * 8);
            std::cerr << "CalcHash: dataLen = " << dataLen
                      << ", bits = " << bits << "\n";

            return key320_hash_calc(
                reinterpret_cast<const std::uint32_t*>(key.data()),
                0,
                reinterpret_cast<const std::uint8_t*>(&tuple),
                bits
            );
        }

        case HashFunc::SIMPLE_XOR:
            return simple_xor_calc(tuple, key, protocol);

        case HashFunc::CRC32:
            assert(false && "CRC32 not implemented");
            return 0;

        case HashFunc::NONE:
        default:
            return 0;
    }
}

} // namespace vnic::rss::soft
