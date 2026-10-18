#include "rss/hash.h"

#include <array>
#include <cstring>
#include <algorithm>

#include <netinet/in.h>

namespace vnic::rss::soft {

namespace {

constexpr std::size_t kMaxHashInputBytes{16 + 16 + 2 + 2 + 1}; // IPv6: src + dst + ports + proto
constexpr std::size_t kMinKeyBytes{40};

bool HasIp(Protocol protocol) {
    const auto p{static_cast<std::uint32_t>(protocol)};
    return (p & (static_cast<std::uint32_t>(Protocol::IPV4) | static_cast<std::uint32_t>(Protocol::IPV6))) != 0;
}

bool HasL4(Protocol protocol) {
    const auto p{static_cast<std::uint32_t>(protocol)};
    return (p & (static_cast<std::uint32_t>(Protocol::TCP) | static_cast<std::uint32_t>(Protocol::UDP) |
                 static_cast<std::uint32_t>(Protocol::SCTP))) != 0;
}

// Собирает вход RSS в непрерывный буфер: адреса (4 или 16 байт на адрес),
// затем порты и байт протокола (если выбран L4). Возвращает длину.
std::size_t BuildHashInput(const FiveTuple& tuple, Protocol protocol,
                           std::array<std::uint8_t, kMaxHashInputBytes>& out) {
    if (!HasIp(protocol)) {
        return 0;
    }

    std::size_t offset{0};
    if (tuple.version == FiveTuple::kIpv6) {
        std::memcpy(out.data() + offset, &tuple.saddr6, sizeof(in6_addr));
        offset += sizeof(in6_addr);
        std::memcpy(out.data() + offset, &tuple.daddr6, sizeof(in6_addr));
        offset += sizeof(in6_addr);
    } else {
        std::memcpy(out.data() + offset, &tuple.saddr4, sizeof(__be32));
        offset += sizeof(__be32);
        std::memcpy(out.data() + offset, &tuple.daddr4, sizeof(__be32));
        offset += sizeof(__be32);
    }

    if (HasL4(protocol)) {
        std::memcpy(out.data() + offset, &tuple.sport, sizeof(__be16));
        offset += sizeof(__be16);
        std::memcpy(out.data() + offset, &tuple.dport, sizeof(__be16));
        offset += sizeof(__be16);
        out[offset++] = tuple.iproto;
    }

    return offset;
}

// ============================================================
//  TOEPLITZ (320-bit key, 10 x 32-bit words)
// ============================================================
// Ключ читается через memcpy: key может быть не выровнен под uint32.
std::uint32_t key320_hash_calc(const std::uint8_t* key, std::uint32_t hash, const std::uint8_t* data, int bits) {
    std::size_t keyOffset = 0;

    for (int i = 0; i < bits; ++i) {
        const bool bit = (data[i >> 3] >> (7 - (i & 7))) & 1;

        if (bit) {
            std::uint32_t word;
            std::memcpy(&word, key + keyOffset, sizeof(word));
            hash ^= word;
        }

        keyOffset += 4;
        if (keyOffset + 4 > 40) {
            keyOffset = 0; // циклический ключ
        }
    }

    return hash;
}

// ============================================================
//  ПРОСТОЙ XOR (по байтам входа RSS, с учётом protocol через длину)
// ============================================================
std::uint32_t simple_xor_calc(const std::uint8_t* data, std::size_t len, std::span<const std::uint8_t> key) {
    std::uint32_t hash = 0;

    for (std::size_t i = 0; i < len; ++i) {
        hash ^= static_cast<std::uint32_t>(data[i]) << ((i % 4) * 8);
    }

    // Ключ домешиваем, чтобы результат зависел от RSS-ключа.
    if (key.size() >= 4) {
        hash ^= static_cast<std::uint32_t>(key[0]) | (static_cast<std::uint32_t>(key[1]) << 8) |
                (static_cast<std::uint32_t>(key[2]) << 16) | (static_cast<std::uint32_t>(key[3]) << 24);
    }
    return hash;
}

// ============================================================
//  CRC32 (IEEE 802.3, отражённый полином 0xEDB88320)
// ============================================================
// Хэширует тот же вход 5-tuple, что и Toeplitz. Стартовое значение берётся из
// RSS-ключа (первые 4 байта), поэтому ключ влияет на результат, но сам
// полином — стандартный IEEE.
std::uint32_t crc32_calc(const std::uint8_t* data, std::size_t len, std::uint32_t seed) {
    std::uint32_t crc = seed;
    for (std::size_t i = 0; i < len; ++i) {
        crc ^= data[i];
        for (int bit = 0; bit < 8; ++bit) {
            const std::uint32_t mask = 0u - (crc & 1u);
            crc                      = (crc >> 1) ^ (0xEDB88320u & mask);
        }
    }
    return ~crc;
}

// ============================================================
//  СИММЕТРИЧНАЯ ПРЕДОБРАБОТКА
// ============================================================
// Сортирует IP и порты так, чтобы A→B и B→A давали ОДИН вход.
void MakeSymmetric(FiveTuple& tuple) {
    if (tuple.version == FiveTuple::kIpv6) {
        const int cmp = std::memcmp(&tuple.saddr6, &tuple.daddr6, sizeof(in6_addr));
        if (cmp > 0) {
            std::swap(tuple.saddr6, tuple.daddr6);
            std::swap(tuple.sport, tuple.dport);
        } else if (cmp == 0 && ntohs(tuple.sport) > ntohs(tuple.dport)) {
            std::swap(tuple.sport, tuple.dport);
        }
        return;
    }

    const auto srcIp = ntohl(tuple.saddr4);
    const auto dstIp = ntohl(tuple.daddr4);
    if (srcIp > dstIp) {
        std::swap(tuple.saddr4, tuple.daddr4);
        std::swap(tuple.sport, tuple.dport);
    } else if (srcIp == dstIp) {
        if (ntohs(tuple.sport) > ntohs(tuple.dport)) {
            std::swap(tuple.sport, tuple.dport);
        }
    }
}

} // namespace

// ============================================================
//  CALC HASH
// ============================================================
std::uint32_t CalcHash(FiveTuple tuple, std::span<const std::uint8_t> key, HashFunc hf, Protocol protocol,
                       SymmetryMode symmetry) {
    if (symmetry == SymmetryMode::SYMMETRIC) {
        MakeSymmetric(tuple);
    }

    std::array<std::uint8_t, kMaxHashInputBytes> input{};
    const auto inputLen{BuildHashInput(tuple, protocol, input)};
    if (inputLen == 0) {
        return 0;
    }

    switch (hf) {
    case HashFunc::TOEPLITZ: {
        // Toeplitz требует 320-битный (40-байтовый) ключ; при коротком —
        // не читаем за границы span, а возвращаем детерминированный 0.
        if (key.size() < kMinKeyBytes) {
            return 0;
        }

        return key320_hash_calc(key.data(), 0, input.data(), static_cast<int>(inputLen * 8));
    }

    case HashFunc::SIMPLE_XOR: return simple_xor_calc(input.data(), inputLen, key);

    case HashFunc::CRC32: {
        std::uint32_t seed{0xFFFFFFFFu};
        if (key.size() >= 4) {
            seed ^= static_cast<std::uint32_t>(key[0]) | (static_cast<std::uint32_t>(key[1]) << 8) |
                    (static_cast<std::uint32_t>(key[2]) << 16) | (static_cast<std::uint32_t>(key[3]) << 24);
        }

        return crc32_calc(input.data(), inputLen, seed);
    }

    case HashFunc::NONE:
    default: return 0;
    }
}

} // namespace vnic::rss::soft
