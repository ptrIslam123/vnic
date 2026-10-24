#include <gtest/gtest.h>

#include "packet/five_tuple.h"
#include "parser/parser.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>
#include <vector>

#include <arpa/inet.h>
#include <linux/if_ether.h>
#include <linux/ip.h>
#include <linux/ipv6.h>
#include <linux/tcp.h>
#include <linux/udp.h>
#include <net/if_arp.h>

namespace {

constexpr std::array<std::uint8_t, 6> kSrcMac{0x00, 0x11, 0x22, 0x33, 0x44, 0x55};
constexpr std::array<std::uint8_t, 6> kDstMac{0x66, 0x77, 0x88, 0x99, 0xAA, 0xBB};

std::span<const std::byte> Bytes(const std::vector<std::byte>& frame) noexcept {
    return std::span<const std::byte>{frame.data(), frame.size()};
}

void WriteEthernet(std::vector<std::byte>& frame, std::uint16_t etherType) {
    ethhdr eth{};
    std::memcpy(eth.h_dest, kDstMac.data(), kDstMac.size());
    std::memcpy(eth.h_source, kSrcMac.data(), kSrcMac.size());
    eth.h_proto = htons(etherType);
    std::memcpy(frame.data(), &eth, sizeof(eth));
}

std::vector<std::byte> MakeIpv4Frame(std::uint8_t protocol, std::uint16_t sport, std::uint16_t dport) {
    const std::size_t l4Bytes{protocol == IPPROTO_TCP ? sizeof(tcphdr)
                                                      : (protocol == IPPROTO_UDP ? sizeof(udphdr) : std::size_t{0})};
    std::vector<std::byte> frame(sizeof(ethhdr) + sizeof(iphdr) + l4Bytes, std::byte{0});
    WriteEthernet(frame, ETH_P_IP);

    iphdr ip{};
    ip.version  = 4;
    ip.ihl      = 5;
    ip.ttl      = 64;
    ip.protocol = protocol;
    ip.tot_len  = htons(static_cast<std::uint16_t>(sizeof(iphdr) + l4Bytes));
    ip.saddr    = htonl(0x0A000001);
    ip.daddr    = htonl(0x0A000002);
    std::memcpy(frame.data() + sizeof(ethhdr), &ip, sizeof(ip));

    if (protocol == IPPROTO_TCP) {
        tcphdr tcp{};
        tcp.source = htons(sport);
        tcp.dest   = htons(dport);
        std::memcpy(frame.data() + sizeof(ethhdr) + sizeof(iphdr), &tcp, sizeof(tcp));
    } else if (protocol == IPPROTO_UDP) {
        udphdr udp{};
        udp.source = htons(sport);
        udp.dest   = htons(dport);
        udp.len    = htons(sizeof(udphdr));
        std::memcpy(frame.data() + sizeof(ethhdr) + sizeof(iphdr), &udp, sizeof(udp));
    }

    return frame;
}

std::vector<std::byte> MakeIpv6Frame(std::uint8_t nextHeader, std::uint16_t sport, std::uint16_t dport) {
    const std::size_t l4Bytes{
        nextHeader == IPPROTO_TCP ? sizeof(tcphdr) : (nextHeader == IPPROTO_UDP ? sizeof(udphdr) : std::size_t{0})};
    std::vector<std::byte> frame(sizeof(ethhdr) + sizeof(ipv6hdr) + l4Bytes, std::byte{0});
    WriteEthernet(frame, ETH_P_IPV6);

    ipv6hdr ip6{};
    ip6.version     = 6;
    ip6.nexthdr     = nextHeader;
    ip6.hop_limit   = 64;
    ip6.payload_len = htons(static_cast<std::uint16_t>(l4Bytes));
    EXPECT_EQ(inet_pton(AF_INET6, "2001:db8::1", &ip6.saddr), 1);
    EXPECT_EQ(inet_pton(AF_INET6, "2001:db8::2", &ip6.daddr), 1);
    std::memcpy(frame.data() + sizeof(ethhdr), &ip6, sizeof(ip6));

    if (nextHeader == IPPROTO_TCP) {
        tcphdr tcp{};
        tcp.source = htons(sport);
        tcp.dest   = htons(dport);
        std::memcpy(frame.data() + sizeof(ethhdr) + sizeof(ipv6hdr), &tcp, sizeof(tcp));
    } else if (nextHeader == IPPROTO_UDP) {
        udphdr udp{};
        udp.source = htons(sport);
        udp.dest   = htons(dport);
        udp.len    = htons(sizeof(udphdr));
        std::memcpy(frame.data() + sizeof(ethhdr) + sizeof(ipv6hdr), &udp, sizeof(udp));
    }

    return frame;
}

std::vector<std::byte> MakeArpFrame() {
    std::vector<std::byte> frame(sizeof(ethhdr) + sizeof(arphdr) + 20, std::byte{0});
    WriteEthernet(frame, ETH_P_ARP);

    arphdr arp{};
    arp.ar_hrd = htons(ARPHRD_ETHER);
    arp.ar_pro = htons(ETH_P_IP);
    arp.ar_hln = 6;
    arp.ar_pln = 4;
    arp.ar_op  = htons(1);
    std::memcpy(frame.data() + sizeof(ethhdr), &arp, sizeof(arp));

    return frame;
}

} // namespace

TEST(FiveTupleTest, Ipv4TcpPopulatesAddressesPortsAndProtocol) {
    const auto frame = MakeIpv4Frame(IPPROTO_TCP, 12345, 443);
    vnic::Parser parser;
    ASSERT_TRUE(parser.parse(Bytes(frame)).has_value());

    const auto tuple = vnic::Get5Tuple(parser);

    __be32 expectedSrc{};
    __be32 expectedDst{};
    ASSERT_EQ(inet_pton(AF_INET, "10.0.0.1", &expectedSrc), 1);
    ASSERT_EQ(inet_pton(AF_INET, "10.0.0.2", &expectedDst), 1);

    EXPECT_EQ(tuple.version, vnic::FiveTuple::kIpv4);
    EXPECT_EQ(tuple.saddr4, expectedSrc);
    EXPECT_EQ(tuple.daddr4, expectedDst);
    EXPECT_EQ(tuple.sport, htons(12345));
    EXPECT_EQ(tuple.dport, htons(443));
    EXPECT_EQ(tuple.iproto, IPPROTO_TCP);
}

TEST(FiveTupleTest, Ipv4UdpPopulatesAddressesPortsAndProtocol) {
    const auto frame = MakeIpv4Frame(IPPROTO_UDP, 5353, 53);
    vnic::Parser parser;
    ASSERT_TRUE(parser.parse(Bytes(frame)).has_value());

    const auto tuple = vnic::Get5Tuple(parser);

    EXPECT_EQ(tuple.version, vnic::FiveTuple::kIpv4);
    EXPECT_EQ(tuple.sport, htons(5353));
    EXPECT_EQ(tuple.dport, htons(53));
    EXPECT_EQ(tuple.iproto, IPPROTO_UDP);
}

TEST(FiveTupleTest, Ipv4UnknownProtocolKeepsAddressesAndZeroPorts) {
    constexpr std::uint8_t kUnknownProtocol{99};
    const auto frame = MakeIpv4Frame(kUnknownProtocol, 12345, 443);
    vnic::Parser parser;
    ASSERT_TRUE(parser.parse(Bytes(frame)).has_value());

    const auto tuple = vnic::Get5Tuple(parser);

    EXPECT_EQ(tuple.version, vnic::FiveTuple::kIpv4);
    EXPECT_EQ(tuple.saddr4, htonl(0x0A000001));
    EXPECT_EQ(tuple.daddr4, htonl(0x0A000002));
    EXPECT_EQ(tuple.sport, 0);
    EXPECT_EQ(tuple.dport, 0);
    EXPECT_EQ(tuple.iproto, kUnknownProtocol);
}

TEST(FiveTupleTest, Ipv4TcpPreservesZeroPorts) {
    const auto frame = MakeIpv4Frame(IPPROTO_TCP, 0, 0);
    vnic::Parser parser;
    ASSERT_TRUE(parser.parse(Bytes(frame)).has_value());

    const auto tuple = vnic::Get5Tuple(parser);

    EXPECT_EQ(tuple.version, vnic::FiveTuple::kIpv4);
    EXPECT_EQ(tuple.sport, 0);
    EXPECT_EQ(tuple.dport, 0);
    EXPECT_EQ(tuple.iproto, IPPROTO_TCP);
}

TEST(FiveTupleTest, Ipv6TcpPopulatesAddressesPortsAndProtocol) {
    const auto frame = MakeIpv6Frame(IPPROTO_TCP, 12345, 443);
    vnic::Parser parser;
    ASSERT_TRUE(parser.parse(Bytes(frame)).has_value());

    const auto tuple = vnic::Get5Tuple(parser);

    in6_addr expectedSrc{};
    in6_addr expectedDst{};
    ASSERT_EQ(inet_pton(AF_INET6, "2001:db8::1", &expectedSrc), 1);
    ASSERT_EQ(inet_pton(AF_INET6, "2001:db8::2", &expectedDst), 1);

    EXPECT_EQ(tuple.version, vnic::FiveTuple::kIpv6);
    EXPECT_EQ(std::memcmp(&tuple.saddr6, &expectedSrc, sizeof(in6_addr)), 0);
    EXPECT_EQ(std::memcmp(&tuple.daddr6, &expectedDst, sizeof(in6_addr)), 0);
    EXPECT_EQ(tuple.sport, htons(12345));
    EXPECT_EQ(tuple.dport, htons(443));
    EXPECT_EQ(tuple.iproto, IPPROTO_TCP);
}

TEST(FiveTupleTest, Ipv6UdpPopulatesAddressesPortsAndProtocol) {
    const auto frame = MakeIpv6Frame(IPPROTO_UDP, 5353, 53);
    vnic::Parser parser;
    ASSERT_TRUE(parser.parse(Bytes(frame)).has_value());

    const auto tuple = vnic::Get5Tuple(parser);

    EXPECT_EQ(tuple.version, vnic::FiveTuple::kIpv6);
    EXPECT_EQ(tuple.sport, htons(5353));
    EXPECT_EQ(tuple.dport, htons(53));
    EXPECT_EQ(tuple.iproto, IPPROTO_UDP);
}

TEST(FiveTupleTest, Ipv6UnknownNextHeaderKeepsAddressesAndZeroPorts) {
    constexpr std::uint8_t kUnknownNextHeader{99};
    const auto frame = MakeIpv6Frame(kUnknownNextHeader, 12345, 443);
    vnic::Parser parser;
    ASSERT_TRUE(parser.parse(Bytes(frame)).has_value());

    const auto tuple = vnic::Get5Tuple(parser);

    EXPECT_EQ(tuple.version, vnic::FiveTuple::kIpv6);
    EXPECT_EQ(tuple.sport, 0);
    EXPECT_EQ(tuple.dport, 0);
    EXPECT_EQ(tuple.iproto, kUnknownNextHeader);
}

TEST(FiveTupleTest, NonIpFrameReturnsZeroTuple) {
    const auto frame = MakeArpFrame();
    vnic::Parser parser;
    ASSERT_TRUE(parser.parse(Bytes(frame)).has_value());

    const auto tuple = vnic::Get5Tuple(parser);
    const vnic::FiveTuple empty{};

    EXPECT_EQ(tuple.version, empty.version);
    EXPECT_EQ(tuple.saddr4, empty.saddr4);
    EXPECT_EQ(tuple.daddr4, empty.daddr4);
    EXPECT_EQ(std::memcmp(&tuple.saddr6, &empty.saddr6, sizeof(in6_addr)), 0);
    EXPECT_EQ(std::memcmp(&tuple.daddr6, &empty.daddr6, sizeof(in6_addr)), 0);
    EXPECT_EQ(tuple.sport, empty.sport);
    EXPECT_EQ(tuple.dport, empty.dport);
    EXPECT_EQ(tuple.iproto, empty.iproto);
}

TEST(FiveTupleTest, UnparsedFrameReturnsZeroTuple) {
    vnic::Parser parser;
    const auto tuple = vnic::Get5Tuple(parser);
    const vnic::FiveTuple empty{};

    EXPECT_EQ(tuple.version, empty.version);
    EXPECT_EQ(tuple.saddr4, empty.saddr4);
    EXPECT_EQ(tuple.daddr4, empty.daddr4);
    EXPECT_EQ(tuple.sport, empty.sport);
    EXPECT_EQ(tuple.dport, empty.dport);
    EXPECT_EQ(tuple.iproto, empty.iproto);
}
