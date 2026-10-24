#include <gtest/gtest.h>

#include "packet/five_tuple.h"
#include "rss/hash.h"

#include <array>
#include <cstdint>
#include <span>

#include <arpa/inet.h>

namespace {

constexpr std::array<std::uint8_t, 40> kDefaultKey{
    0x6d, 0x5a, 0x56, 0xda, 0x25, 0x5b, 0x0e, 0xc2, 0x41, 0x67, 0x25, 0x3d, 0x43, 0xa3,
    0x8f, 0xb0, 0xd0, 0xca, 0x2b, 0xcb, 0xae, 0x7b, 0x30, 0xb4, 0x77, 0xcb, 0x2d, 0xa3,
    0x80, 0x30, 0xf2, 0x0c, 0x6a, 0x42, 0xb7, 0x3b, 0xbe, 0xac, 0x01, 0xfa,
};

constexpr auto kIpv4Tcp = static_cast<vnic::rss::Protocol>(static_cast<std::uint32_t>(vnic::rss::Protocol::IPV4) |
                                                           static_cast<std::uint32_t>(vnic::rss::Protocol::TCP));
constexpr auto kIpv6Tcp = static_cast<vnic::rss::Protocol>(static_cast<std::uint32_t>(vnic::rss::Protocol::IPV6) |
                                                           static_cast<std::uint32_t>(vnic::rss::Protocol::TCP));

vnic::FiveTuple MakeIpv4(const char* src, const char* dst, std::uint16_t sport, std::uint16_t dport) {
    vnic::FiveTuple tuple{};
    tuple.version = vnic::FiveTuple::kIpv4;
    EXPECT_EQ(inet_pton(AF_INET, src, &tuple.saddr4), 1);
    EXPECT_EQ(inet_pton(AF_INET, dst, &tuple.daddr4), 1);
    tuple.sport  = htons(sport);
    tuple.dport  = htons(dport);
    tuple.iproto = IPPROTO_TCP;
    return tuple;
}

vnic::FiveTuple MakeIpv6(const char* src, const char* dst, std::uint16_t sport, std::uint16_t dport) {
    vnic::FiveTuple tuple{};
    tuple.version = vnic::FiveTuple::kIpv6;
    EXPECT_EQ(inet_pton(AF_INET6, src, &tuple.saddr6), 1);
    EXPECT_EQ(inet_pton(AF_INET6, dst, &tuple.daddr6), 1);
    tuple.sport  = htons(sport);
    tuple.dport  = htons(dport);
    tuple.iproto = IPPROTO_TCP;
    return tuple;
}

std::uint32_t Hash(const vnic::FiveTuple& tuple, vnic::rss::Protocol protocol) {
    return vnic::rss::soft::CalcHash(tuple, kDefaultKey, vnic::rss::HashFunc::TOEPLITZ, protocol,
                                     vnic::rss::SymmetryMode::ASYMMETRIC);
}

std::uint32_t HashWith(const vnic::FiveTuple& tuple, vnic::rss::HashFunc hf, vnic::rss::Protocol protocol,
                       vnic::rss::SymmetryMode symmetry, std::span<const std::uint8_t> key) {
    return vnic::rss::soft::CalcHash(tuple, key, hf, protocol, symmetry);
}

struct Ipv4Vector {
    const char* src;
    const char* dst;
    std::uint16_t sport;
    std::uint16_t dport;
    std::uint32_t l3;
    std::uint32_t l3l4;
};

// From the Intel 82599 datasheet RSS verification suite (also used by DPDK).
constexpr std::array<Ipv4Vector, 5> kIpv4Vectors{{
    {"66.9.149.187", "161.142.100.80", 2794, 1766, 0x323e8fc2u, 0x51ccc178u},
    {"199.92.111.2", "65.69.140.83", 14230, 4739, 0xd718262au, 0xc626b0eau},
    {"24.19.198.95", "12.22.207.184", 12898, 38024, 0xd2d0a5deu, 0x5c2b394au},
    {"38.27.205.30", "209.142.163.6", 48228, 2217, 0x82989176u, 0xafc7327fu},
    {"153.39.163.191", "202.188.127.2", 44251, 1303, 0x5d1809c5u, 0x10e828a2u},
}};

TEST(RssToeplitzTest, MatchesIpv4ReferenceVectors) {
    for (const auto& vector : kIpv4Vectors) {
        SCOPED_TRACE(vector.src);
        const auto tuple = MakeIpv4(vector.src, vector.dst, vector.sport, vector.dport);

        EXPECT_EQ(Hash(tuple, vnic::rss::Protocol::IPV4), vector.l3);
        EXPECT_EQ(Hash(tuple, kIpv4Tcp), vector.l3l4);
        EXPECT_EQ(Hash(tuple, vnic::rss::Protocol::ALL), vector.l3l4);
    }
}

struct Ipv6Vector {
    const char* src;
    const char* dst;
    std::uint16_t sport;
    std::uint16_t dport;
    std::uint32_t l3;
    std::uint32_t l3l4;
};

constexpr std::array<Ipv6Vector, 2> kIpv6Vectors{{
    {"3ffe:2501:200:1fff::7", "3ffe:2501:200:3::1", 2794, 1766, 0x2cc18cd5u, 0x40207d3du},
    {"3ffe:501:8::260:97ff:fe40:efab", "ff02::1", 14230, 4739, 0x0f0c461cu, 0xdde51bbfu},
}};

TEST(RssToeplitzTest, MatchesIpv6ReferenceVectors) {
    for (const auto& vector : kIpv6Vectors) {
        SCOPED_TRACE(vector.src);
        const auto tuple = MakeIpv6(vector.src, vector.dst, vector.sport, vector.dport);

        EXPECT_EQ(Hash(tuple, vnic::rss::Protocol::IPV6), vector.l3);
        EXPECT_EQ(Hash(tuple, kIpv6Tcp), vector.l3l4);
        EXPECT_EQ(Hash(tuple, vnic::rss::Protocol::ALL), vector.l3l4);
    }
}

TEST(RssToeplitzTest, SymmetricModeCanonicalizesDirection) {
    const auto forward  = MakeIpv4("10.0.0.1", "10.0.0.2", 12345, 443);
    const auto backward = MakeIpv4("10.0.0.2", "10.0.0.1", 443, 12345);

    const auto symmetricForward  = vnic::rss::soft::CalcHash(forward, kDefaultKey, vnic::rss::HashFunc::TOEPLITZ,
                                                             kIpv4Tcp, vnic::rss::SymmetryMode::SYMMETRIC);
    const auto symmetricBackward = vnic::rss::soft::CalcHash(backward, kDefaultKey, vnic::rss::HashFunc::TOEPLITZ,
                                                             kIpv4Tcp, vnic::rss::SymmetryMode::SYMMETRIC);

    EXPECT_EQ(symmetricForward, symmetricBackward);
}

TEST(RssToeplitzTest, RequiresFortyByteKey) {
    const auto tuple = MakeIpv4("10.0.0.1", "10.0.0.2", 12345, 443);
    const std::array<std::uint8_t, 39> shortKey{};

    EXPECT_EQ(vnic::rss::soft::CalcHash(tuple, shortKey, vnic::rss::HashFunc::TOEPLITZ, kIpv4Tcp,
                                        vnic::rss::SymmetryMode::ASYMMETRIC),
              0u);
}

TEST(RssHashTest, NoneFunctionReturnsZero) {
    const auto tuple = MakeIpv4("10.0.0.1", "10.0.0.2", 12345, 443);

    EXPECT_EQ(vnic::rss::soft::CalcHash(tuple, kDefaultKey, vnic::rss::HashFunc::NONE, kIpv4Tcp,
                                        vnic::rss::SymmetryMode::ASYMMETRIC),
              0u);
}

TEST(RssSimpleXorTest, MatchesIpv4ReferenceVectors) {
    const auto tuple = MakeIpv4("10.0.0.1", "10.0.0.2", 12345, 443);

    EXPECT_EQ(HashWith(tuple, vnic::rss::HashFunc::SIMPLE_XOR, vnic::rss::Protocol::IPV4,
                       vnic::rss::SymmetryMode::ASYMMETRIC, kDefaultKey),
              0xd9565a6du);
    EXPECT_EQ(HashWith(tuple, vnic::rss::HashFunc::SIMPLE_XOR, vnic::rss::Protocol::ALL,
                       vnic::rss::SymmetryMode::ASYMMETRIC, kDefaultKey),
              0x6257635du);
}

TEST(RssCrc32Test, MatchesIpv4ReferenceVectors) {
    const auto tuple = MakeIpv4("10.0.0.1", "10.0.0.2", 12345, 443);

    EXPECT_EQ(HashWith(tuple, vnic::rss::HashFunc::CRC32, vnic::rss::Protocol::IPV4,
                       vnic::rss::SymmetryMode::ASYMMETRIC, kDefaultKey),
              0x9de8e4d8u);
    EXPECT_EQ(HashWith(tuple, vnic::rss::HashFunc::CRC32, vnic::rss::Protocol::ALL, vnic::rss::SymmetryMode::ASYMMETRIC,
                       kDefaultKey),
              0xea6fddf0u);
}

TEST(RssHashTest, AlternativeHashFunctionsUseOnlyFirstFourKeyBytes) {
    const auto tuple = MakeIpv4("10.0.0.1", "10.0.0.2", 12345, 443);
    auto alternativeKey{kDefaultKey};
    alternativeKey[0] ^= 0xFF;
    auto extendedKey{kDefaultKey};
    extendedKey[4] ^= 0xFF;

    for (const auto hf : {vnic::rss::HashFunc::SIMPLE_XOR, vnic::rss::HashFunc::CRC32}) {
        const auto baseline =
            HashWith(tuple, hf, vnic::rss::Protocol::ALL, vnic::rss::SymmetryMode::ASYMMETRIC, kDefaultKey);
        const auto changed =
            HashWith(tuple, hf, vnic::rss::Protocol::ALL, vnic::rss::SymmetryMode::ASYMMETRIC, alternativeKey);
        const auto extended =
            HashWith(tuple, hf, vnic::rss::Protocol::ALL, vnic::rss::SymmetryMode::ASYMMETRIC, extendedKey);

        EXPECT_NE(changed, baseline);
        EXPECT_EQ(extended, baseline);
    }
}

TEST(RssHashTest, AlternativeHashFunctionsHandleShortKeysAndMissingIp) {
    const auto tuple = MakeIpv4("10.0.0.1", "10.0.0.2", 12345, 443);
    const std::array<std::uint8_t, 2> shortKey{0x12, 0x34};

    EXPECT_EQ(HashWith(tuple, vnic::rss::HashFunc::SIMPLE_XOR, vnic::rss::Protocol::IPV4,
                       vnic::rss::SymmetryMode::ASYMMETRIC, shortKey),
              0x03000000u);
    EXPECT_EQ(HashWith(tuple, vnic::rss::HashFunc::CRC32, vnic::rss::Protocol::IPV4,
                       vnic::rss::SymmetryMode::ASYMMETRIC, shortKey),
              0x48316b59u);

    for (const auto hf : {vnic::rss::HashFunc::TOEPLITZ, vnic::rss::HashFunc::SIMPLE_XOR, vnic::rss::HashFunc::CRC32}) {
        EXPECT_EQ(HashWith(tuple, hf, vnic::rss::Protocol::NONE, vnic::rss::SymmetryMode::ASYMMETRIC, kDefaultKey), 0u);
        EXPECT_EQ(HashWith(tuple, hf, vnic::rss::Protocol::TCP, vnic::rss::SymmetryMode::ASYMMETRIC, kDefaultKey), 0u);
    }
}

TEST(RssHashTest, SymmetricModeCanonicalizesIpv6ForAllHashFunctions) {
    const auto forward  = MakeIpv6("2001:db8::1", "2001:db8::2", 12345, 443);
    const auto backward = MakeIpv6("2001:db8::2", "2001:db8::1", 443, 12345);

    for (const auto hf : {vnic::rss::HashFunc::TOEPLITZ, vnic::rss::HashFunc::SIMPLE_XOR, vnic::rss::HashFunc::CRC32}) {
        EXPECT_EQ(HashWith(forward, hf, kIpv6Tcp, vnic::rss::SymmetryMode::SYMMETRIC, kDefaultKey),
                  HashWith(backward, hf, kIpv6Tcp, vnic::rss::SymmetryMode::SYMMETRIC, kDefaultKey));
    }
}

TEST(RssHashTest, SymmetricModeUsesPortsWhenAddressesMatch) {
    const auto ipv4Forward  = MakeIpv4("10.0.0.1", "10.0.0.1", 5000, 80);
    const auto ipv4Backward = MakeIpv4("10.0.0.1", "10.0.0.1", 80, 5000);
    const auto ipv6Forward  = MakeIpv6("2001:db8::1", "2001:db8::1", 5000, 80);
    const auto ipv6Backward = MakeIpv6("2001:db8::1", "2001:db8::1", 80, 5000);

    for (const auto hf : {vnic::rss::HashFunc::TOEPLITZ, vnic::rss::HashFunc::SIMPLE_XOR, vnic::rss::HashFunc::CRC32}) {
        EXPECT_EQ(HashWith(ipv4Forward, hf, kIpv4Tcp, vnic::rss::SymmetryMode::SYMMETRIC, kDefaultKey),
                  HashWith(ipv4Backward, hf, kIpv4Tcp, vnic::rss::SymmetryMode::SYMMETRIC, kDefaultKey));
        EXPECT_EQ(HashWith(ipv6Forward, hf, kIpv6Tcp, vnic::rss::SymmetryMode::SYMMETRIC, kDefaultKey),
                  HashWith(ipv6Backward, hf, kIpv6Tcp, vnic::rss::SymmetryMode::SYMMETRIC, kDefaultKey));
    }
}

} // namespace
