#include <gtest/gtest.h>

#include "parser/parser.h"
#include "utils/unaligned.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <optional>
#include <span>
#include <sstream>
#include <string>
#include <vector>

#include <arpa/inet.h>
#include <linux/icmp.h>
#include <linux/if_ether.h>
#include <linux/ip.h>
#include <linux/ipv6.h>
#include <linux/tcp.h>
#include <linux/udp.h>
#include <net/if_arp.h>
/// TODO: вынести методы создания сырых пакетов в parser.h/.cpp: они пригодятся
/// и в других местах.
///

namespace {

constexpr std::uint8_t kSrcMac[6]{0x00, 0x11, 0x22, 0x33, 0x44, 0x55};
constexpr std::uint8_t kDstMac[6]{0x66, 0x77, 0x88, 0x99, 0xAA, 0xBB};

std::span<const std::byte> Bytes(const std::vector<std::byte>& frame) {
    return std::span<const std::byte>{frame.data(), frame.size()};
}

std::span<const std::byte> Head(const std::vector<std::byte>& frame, std::size_t count) {
    return std::span<const std::byte>{frame.data(), count};
}

std::vector<std::byte> WithEthernet(std::uint16_t ethertype, const std::vector<std::byte>& payload) {
    std::vector<std::byte> frame(sizeof(ethhdr) + payload.size(), std::byte{0});

    auto* eth = reinterpret_cast<ethhdr*>(frame.data());
    std::memcpy(eth->h_dest, kDstMac, sizeof(kDstMac));
    std::memcpy(eth->h_source, kSrcMac, sizeof(kSrcMac));
    eth->h_proto = htons(ethertype);

    if (!payload.empty()) {
        std::memcpy(frame.data() + sizeof(ethhdr), payload.data(), payload.size());
    }

    return frame;
}

// Ethernet with a fixed-size zeroed payload (for size/ethertype-only checks).
std::vector<std::byte> MakeEthernet(std::uint16_t ethertype, std::size_t payloadBytes) {
    return WithEthernet(ethertype, std::vector<std::byte>(payloadBytes, std::byte{0}));
}

// IPv4 header (+ optional L4 payload). `ihl` is in 32-bit words.
std::vector<std::byte> Ipv4Bytes(std::uint8_t ihl, std::uint8_t proto, std::size_t l4Bytes) {
    const std::size_t ipHeaderLen = std::max<std::size_t>(static_cast<std::size_t>(ihl) * 4, sizeof(iphdr));
    std::vector<std::byte> bytes(ipHeaderLen + l4Bytes, std::byte{0});

    auto* ip     = reinterpret_cast<iphdr*>(bytes.data());
    ip->version  = 4;
    ip->ihl      = ihl;
    ip->ttl      = 64;
    ip->protocol = proto;
    ip->tot_len  = htons(static_cast<std::uint16_t>(ipHeaderLen + l4Bytes));
    ip->saddr    = htonl(0x0A000001); // 10.0.0.1
    ip->daddr    = htonl(0x0A000002); // 10.0.0.2

    return bytes;
}

std::vector<std::byte> MakeIpv4(std::uint8_t ihl, std::uint8_t proto, std::size_t l4Bytes) {
    return WithEthernet(ETH_P_IP, Ipv4Bytes(ihl, proto, l4Bytes));
}

// IPv6 header (+ optional L4 payload).
std::vector<std::byte> Ipv6Bytes(std::uint8_t nextHdr, std::size_t l4Bytes) {
    std::vector<std::byte> bytes(sizeof(ipv6hdr) + l4Bytes, std::byte{0});

    auto* ip6        = reinterpret_cast<ipv6hdr*>(bytes.data());
    ip6->version     = 6;
    ip6->nexthdr     = nextHdr;
    ip6->hop_limit   = 64;
    ip6->payload_len = htons(static_cast<std::uint16_t>(l4Bytes));

    return bytes;
}

std::vector<std::byte> MakeIpv6(std::uint8_t nextHdr, std::size_t l4Bytes) {
    return WithEthernet(ETH_P_IPV6, Ipv6Bytes(nextHdr, l4Bytes));
}

// Ethernet + one VLAN tag (TCI + inner EtherType) wrapping an inner L2 payload.
std::vector<std::byte> MakeVlan(std::uint16_t tpid, std::uint16_t innerType, std::uint16_t vlanId,
                                const std::vector<std::byte>& inner) {
    std::vector<std::byte> payload(4 + inner.size(), std::byte{0});

    const std::uint16_t tci   = htons(vlanId);
    const std::uint16_t etype = htons(innerType);
    std::memcpy(payload.data(), &tci, sizeof(tci));
    std::memcpy(payload.data() + 2, &etype, sizeof(etype));
    if (!inner.empty()) {
        std::memcpy(payload.data() + 4, inner.data(), inner.size());
    }

    return WithEthernet(tpid, payload);
}

std::vector<std::byte> MakeArp() {
    constexpr std::size_t kFrameSize{64};
    std::vector<std::byte> frame(kFrameSize, std::byte{0});

    auto* eth = reinterpret_cast<ethhdr*>(frame.data());
    std::memset(eth->h_dest, 0xFF, sizeof(eth->h_dest));
    std::memcpy(eth->h_source, kSrcMac, sizeof(kSrcMac));
    eth->h_proto = htons(ETH_P_ARP);

    auto* arp   = reinterpret_cast<arphdr*>(frame.data() + sizeof(ethhdr));
    arp->ar_hrd = htons(ARPHRD_ETHER);
    arp->ar_pro = htons(ETH_P_IP);
    arp->ar_hln = 6;
    arp->ar_pln = 4;
    arp->ar_op  = htons(1); // request

    return frame;
}

const std::byte* Base(const std::vector<std::byte>& frame) { return frame.data(); }

template <typename T> bool HeaderEq(const std::optional<T>& header, const void* expected) {
    return header.has_value() && std::memcmp(&*header, expected, sizeof(T)) == 0;
}

} // namespace

TEST(ParserTest, DefaultStateIsEmpty) {
    vnic::Parser parser;

    EXPECT_EQ(parser.l1Protocol(), vnic::Parser::L1Protocol::None);
    EXPECT_EQ(parser.l2Protocol(), vnic::Parser::L2Protocol::None);
    EXPECT_EQ(parser.l3Protocol(), vnic::Parser::L3Protocol::None);

    EXPECT_FALSE(parser.eth().has_value());
    EXPECT_FALSE(parser.ipv4().has_value());
    EXPECT_FALSE(parser.ipv6().has_value());
    EXPECT_FALSE(parser.arp().has_value());
    EXPECT_FALSE(parser.icmp().has_value());
    EXPECT_FALSE(parser.tcp().has_value());
    EXPECT_FALSE(parser.udp().has_value());

    EXPECT_EQ(parser.vlanId(), 0u);
    EXPECT_EQ(parser.l2Offset(), 0u);
}

// ---------------------------------------------------------------------------
// L1: Ethernet / VLAN
// ---------------------------------------------------------------------------

TEST(ParserTest, ParsesPlainEthernetIpv4) {
    vnic::Parser parser;
    auto frame = MakeIpv4(5, IPPROTO_UDP, sizeof(udphdr));

    ASSERT_TRUE(parser.parse(Bytes(frame)).has_value());

    EXPECT_EQ(parser.l1Protocol(), vnic::Parser::L1Protocol::Eth);
    EXPECT_TRUE(HeaderEq(parser.eth(), Base(frame)));
    EXPECT_EQ(parser.vlanId(), 0u);
    EXPECT_EQ(parser.l2Offset(), sizeof(ethhdr));
}

TEST(ParserTest, ParsesVlanTaggedIpv4) {
    vnic::Parser parser;
    auto frame = MakeVlan(ETH_P_8021Q, ETH_P_IP, 0x0ABC, Ipv4Bytes(5, IPPROTO_UDP, sizeof(udphdr)));

    ASSERT_TRUE(parser.parse(Bytes(frame)).has_value());

    EXPECT_EQ(parser.l1Protocol(), vnic::Parser::L1Protocol::Vlan);
    EXPECT_TRUE(HeaderEq(parser.eth(), Base(frame)));
    EXPECT_EQ(parser.vlanId(), 0x0ABCu);
    EXPECT_EQ(parser.l2Offset(), sizeof(ethhdr) + 4);
}

TEST(ParserTest, ParsesVlanTaggedWithQinQ) {
    vnic::Parser parser;
    auto frame = MakeVlan(ETH_P_8021AD, ETH_P_IP, 0x0BCD, Ipv4Bytes(5, IPPROTO_UDP, sizeof(udphdr)));

    ASSERT_TRUE(parser.parse(Bytes(frame)).has_value());

    EXPECT_EQ(parser.l1Protocol(), vnic::Parser::L1Protocol::Vlan);
    EXPECT_EQ(parser.vlanId(), 0x0BCDu);
}

TEST(ParserTest, VlanIdIsMaskedToTwelveBits) {
    vnic::Parser parser;
    // 0xF000 sets the priority and DEI bits; only the low 12 bits are the VLAN
    // ID.
    auto frame = MakeVlan(ETH_P_8021Q, ETH_P_IP, 0xF123, Ipv4Bytes(5, IPPROTO_UDP, sizeof(udphdr)));

    ASSERT_TRUE(parser.parse(Bytes(frame)).has_value());

    EXPECT_EQ(parser.vlanId(), 0x0123u);
}

TEST(ParserTest, ParsesVlanTaggedIpv6) {
    vnic::Parser parser;
    auto frame = MakeVlan(ETH_P_8021Q, ETH_P_IPV6, 100, Ipv6Bytes(IPPROTO_UDP, sizeof(udphdr)));

    ASSERT_TRUE(parser.parse(Bytes(frame)).has_value());

    EXPECT_EQ(parser.l1Protocol(), vnic::Parser::L1Protocol::Vlan);
    EXPECT_EQ(parser.l2Protocol(), vnic::Parser::L2Protocol::Ipv6);
    EXPECT_EQ(parser.l2Offset(), sizeof(ethhdr) + 4);
}

TEST(ParserTest, ParsesVlanTaggedArp) {
    std::vector<std::byte> frame(64, std::byte{0});
    auto* eth = reinterpret_cast<ethhdr*>(frame.data());
    std::memset(eth->h_dest, 0xFF, sizeof(eth->h_dest));
    std::memcpy(eth->h_source, kSrcMac, sizeof(kSrcMac));
    eth->h_proto = htons(ETH_P_8021Q);

    const std::uint16_t tci   = htons(7);
    const std::uint16_t inner = htons(ETH_P_ARP);
    std::memcpy(frame.data() + sizeof(ethhdr), &tci, sizeof(tci));
    std::memcpy(frame.data() + sizeof(ethhdr) + 2, &inner, sizeof(inner));

    auto* arp   = reinterpret_cast<arphdr*>(frame.data() + sizeof(ethhdr) + 4);
    arp->ar_hrd = htons(ARPHRD_ETHER);
    arp->ar_pro = htons(ETH_P_IP);
    arp->ar_hln = 6;
    arp->ar_pln = 4;
    arp->ar_op  = htons(1);

    vnic::Parser parser;
    ASSERT_TRUE(parser.parse(Bytes(frame)).has_value());

    EXPECT_EQ(parser.l1Protocol(), vnic::Parser::L1Protocol::Vlan);
    EXPECT_EQ(parser.l2Protocol(), vnic::Parser::L2Protocol::Arp);
    EXPECT_EQ(parser.vlanId(), 7u);
    EXPECT_TRUE(HeaderEq(parser.arp(), frame.data() + sizeof(ethhdr) + 4));
}

TEST(ParserTest, UnknownEtherTypeFailsWithUnknownL2) {
    vnic::Parser parser;
    auto frame = MakeEthernet(0x1234, 32);

    EXPECT_FALSE(parser.parse(Bytes(frame)).has_value());

    EXPECT_EQ(parser.l1Protocol(), vnic::Parser::L1Protocol::Eth);
    EXPECT_TRUE(HeaderEq(parser.eth(), Base(frame)));
    EXPECT_EQ(parser.l2Protocol(), vnic::Parser::L2Protocol::Unknown);
    EXPECT_FALSE(parser.ipv4().has_value());
    EXPECT_FALSE(parser.ipv6().has_value());
    EXPECT_FALSE(parser.arp().has_value());
}

// ---------------------------------------------------------------------------
// L2 / L3: IPv4
// ---------------------------------------------------------------------------

TEST(ParserTest, ParsesIpv4Udp) {
    vnic::Parser parser;
    auto frame  = MakeIpv4(5, IPPROTO_UDP, sizeof(udphdr));
    auto* udp   = reinterpret_cast<udphdr*>(frame.data() + sizeof(ethhdr) + sizeof(iphdr));
    udp->source = htons(53);
    udp->dest   = htons(5353);
    udp->len    = htons(sizeof(udphdr));

    ASSERT_TRUE(parser.parse(Bytes(frame)).has_value());

    EXPECT_EQ(parser.l2Protocol(), vnic::Parser::L2Protocol::Ipv4);
    EXPECT_EQ(parser.l3Protocol(), vnic::Parser::L3Protocol::Udp);
    EXPECT_TRUE(HeaderEq(parser.ipv4(), frame.data() + sizeof(ethhdr)));
    ASSERT_TRUE(parser.udp().has_value());
    EXPECT_TRUE(HeaderEq(parser.udp(), frame.data() + sizeof(ethhdr) + sizeof(iphdr)));
    EXPECT_FALSE(parser.tcp().has_value());
    EXPECT_FALSE(parser.icmp().has_value());
    EXPECT_FALSE(parser.ipv6().has_value());
    EXPECT_FALSE(parser.arp().has_value());
}

TEST(ParserTest, ParsesFromUnalignedBuffer) {
    auto frame = MakeIpv4(5, IPPROTO_UDP, sizeof(udphdr));
    std::vector<std::byte> storage(frame.size() + 1, std::byte{0});
    std::memcpy(storage.data() + 1, frame.data(), frame.size());

    vnic::Parser parser;
    ASSERT_TRUE(parser.parse(std::span<const std::byte>{storage.data() + 1, frame.size()}).has_value());

    const auto ip = parser.ipv4();
    ASSERT_TRUE(ip.has_value());
    EXPECT_EQ(ntohl(ip->saddr), 0x0A000001u);
    EXPECT_EQ(ntohl(ip->daddr), 0x0A000002u);
    ASSERT_TRUE(parser.udp().has_value());
}

TEST(ParserTest, ParsesIpv4Tcp) {
    vnic::Parser parser;
    auto frame = MakeIpv4(5, IPPROTO_TCP, sizeof(tcphdr));

    ASSERT_TRUE(parser.parse(Bytes(frame)).has_value());

    EXPECT_EQ(parser.l3Protocol(), vnic::Parser::L3Protocol::Tcp);
    EXPECT_TRUE(HeaderEq(parser.tcp(), frame.data() + sizeof(ethhdr) + sizeof(iphdr)));
    EXPECT_FALSE(parser.udp().has_value());
    EXPECT_FALSE(parser.icmp().has_value());
}

TEST(ParserTest, ParsesIpv4Icmp) {
    vnic::Parser parser;
    auto frame = MakeIpv4(5, IPPROTO_ICMP, sizeof(icmphdr));
    auto icmp  = vnic::utils::LoadUnaligned<icmphdr>(frame.data() + sizeof(ethhdr) + sizeof(iphdr));
    icmp.type  = ICMP_ECHO;
    icmp.code  = 0;
    vnic::utils::StoreUnaligned(frame.data() + sizeof(ethhdr) + sizeof(iphdr), icmp);

    ASSERT_TRUE(parser.parse(Bytes(frame)).has_value());

    EXPECT_EQ(parser.l3Protocol(), vnic::Parser::L3Protocol::Icmp);
    ASSERT_TRUE(parser.icmp().has_value());
    EXPECT_TRUE(HeaderEq(parser.icmp(), frame.data() + sizeof(ethhdr) + sizeof(iphdr)));
    EXPECT_FALSE(parser.tcp().has_value());
    EXPECT_FALSE(parser.udp().has_value());
}

TEST(ParserTest, IpV4UnknownL4ProtocolIsReportedAsUnknown) {
    vnic::Parser parser;
    auto frame = MakeIpv4(5, 99, 16); // 99 is not TCP/UDP/ICMP

    ASSERT_TRUE(parser.parse(Bytes(frame)).has_value());

    EXPECT_EQ(parser.l2Protocol(), vnic::Parser::L2Protocol::Ipv4);
    EXPECT_EQ(parser.l3Protocol(), vnic::Parser::L3Protocol::Unknown);
    EXPECT_FALSE(parser.tcp().has_value());
    EXPECT_FALSE(parser.udp().has_value());
    EXPECT_FALSE(parser.icmp().has_value());
}

TEST(ParserTest, Ipv4OptionsAreSkippedUsingIhl) {
    vnic::Parser parser;
    auto frame = MakeIpv4(6, IPPROTO_UDP, sizeof(udphdr)); // 24-byte IP header

    ASSERT_TRUE(parser.parse(Bytes(frame)).has_value());

    EXPECT_EQ(parser.l3Protocol(), vnic::Parser::L3Protocol::Udp);
    EXPECT_TRUE(HeaderEq(parser.udp(), frame.data() + sizeof(ethhdr) + 24));
}

// ---------------------------------------------------------------------------
// L2 / L3: IPv6
// ---------------------------------------------------------------------------

TEST(ParserTest, ParsesIpv6Udp) {
    vnic::Parser parser;
    auto frame = MakeIpv6(IPPROTO_UDP, sizeof(udphdr));

    ASSERT_TRUE(parser.parse(Bytes(frame)).has_value());

    EXPECT_EQ(parser.l2Protocol(), vnic::Parser::L2Protocol::Ipv6);
    EXPECT_EQ(parser.l3Protocol(), vnic::Parser::L3Protocol::Udp);
    EXPECT_TRUE(HeaderEq(parser.ipv6(), frame.data() + sizeof(ethhdr)));
    EXPECT_TRUE(HeaderEq(parser.udp(), frame.data() + sizeof(ethhdr) + sizeof(ipv6hdr)));
    EXPECT_FALSE(parser.ipv4().has_value());
    EXPECT_FALSE(parser.arp().has_value());
}

TEST(ParserTest, ParsesIpv6Tcp) {
    vnic::Parser parser;
    auto frame = MakeIpv6(IPPROTO_TCP, sizeof(tcphdr));

    ASSERT_TRUE(parser.parse(Bytes(frame)).has_value());

    EXPECT_EQ(parser.l3Protocol(), vnic::Parser::L3Protocol::Tcp);
    EXPECT_TRUE(HeaderEq(parser.tcp(), frame.data() + sizeof(ethhdr) + sizeof(ipv6hdr)));
}

TEST(ParserTest, ParsesIpv6Icmp) {
    vnic::Parser parser;
    auto frame = MakeIpv6(IPPROTO_ICMPV6, sizeof(icmphdr));

    ASSERT_TRUE(parser.parse(Bytes(frame)).has_value());

    EXPECT_EQ(parser.l3Protocol(), vnic::Parser::L3Protocol::Icmp);
    ASSERT_TRUE(parser.icmp().has_value());
}

TEST(ParserTest, Ipv6UnknownNextHeaderIsReportedAsUnknown) {
    vnic::Parser parser;
    auto frame = MakeIpv6(99, 16);

    ASSERT_TRUE(parser.parse(Bytes(frame)).has_value());

    EXPECT_EQ(parser.l2Protocol(), vnic::Parser::L2Protocol::Ipv6);
    EXPECT_EQ(parser.l3Protocol(), vnic::Parser::L3Protocol::Unknown);
    EXPECT_FALSE(parser.tcp().has_value());
    EXPECT_FALSE(parser.udp().has_value());
    EXPECT_FALSE(parser.icmp().has_value());
}

// ---------------------------------------------------------------------------
// L2: ARP
// ---------------------------------------------------------------------------

TEST(ParserTest, ParsesArp) {
    vnic::Parser parser;
    auto frame = MakeArp();

    ASSERT_TRUE(parser.parse(Bytes(frame)).has_value());

    EXPECT_EQ(parser.l1Protocol(), vnic::Parser::L1Protocol::Eth);
    EXPECT_EQ(parser.l2Protocol(), vnic::Parser::L2Protocol::Arp);
    EXPECT_TRUE(HeaderEq(parser.arp(), frame.data() + sizeof(ethhdr)));
    EXPECT_FALSE(parser.ipv4().has_value());
    EXPECT_FALSE(parser.ipv6().has_value());
    EXPECT_EQ(parser.l3Protocol(), vnic::Parser::L3Protocol::None);
}

// ---------------------------------------------------------------------------
// Malformed / truncated input
// ---------------------------------------------------------------------------

TEST(ParserTest, EmptyBufferFailsWithoutStateChange) {
    vnic::Parser parser;
    std::vector<std::byte> empty;

    EXPECT_FALSE(parser.parse(Bytes(empty)).has_value());

    EXPECT_EQ(parser.l1Protocol(), vnic::Parser::L1Protocol::None);
    EXPECT_FALSE(parser.eth().has_value());
    EXPECT_EQ(parser.l2Offset(), 0u);
}

TEST(ParserTest, TruncatedEthernetHeaderFails) {
    vnic::Parser parser;
    auto frame = MakeEthernet(ETH_P_IP, 64);

    EXPECT_FALSE(parser.parse(Head(frame, sizeof(ethhdr) - 1)).has_value());

    EXPECT_EQ(parser.l1Protocol(), vnic::Parser::L1Protocol::None);
    EXPECT_FALSE(parser.eth().has_value());
}

TEST(ParserTest, TruncatedVlanTagFails) {
    vnic::Parser parser;
    auto frame = MakeVlan(ETH_P_8021Q, ETH_P_IP, 5, Ipv4Bytes(5, IPPROTO_UDP, sizeof(udphdr)));

    // Only 3 of the 4 required VLAN bytes are present.
    EXPECT_FALSE(parser.parse(Head(frame, sizeof(ethhdr) + 3)).has_value());

    EXPECT_EQ(parser.l1Protocol(), vnic::Parser::L1Protocol::Eth);
    EXPECT_EQ(parser.vlanId(), 0u);
}

TEST(ParserTest, TruncatedIpv4HeaderFails) {
    vnic::Parser parser;
    auto frame = MakeIpv4(5, IPPROTO_UDP, sizeof(udphdr));

    EXPECT_FALSE(parser.parse(Head(frame, sizeof(ethhdr) + sizeof(iphdr) - 1)).has_value());

    EXPECT_EQ(parser.l1Protocol(), vnic::Parser::L1Protocol::Eth);
    EXPECT_EQ(parser.l2Protocol(), vnic::Parser::L2Protocol::None);
    EXPECT_FALSE(parser.ipv4().has_value());
}

TEST(ParserTest, Ipv4WithBadVersionFails) {
    vnic::Parser parser;
    auto frame = MakeIpv4(5, IPPROTO_UDP, sizeof(udphdr));
    auto ip    = vnic::utils::LoadUnaligned<iphdr>(frame.data() + sizeof(ethhdr));
    ip.version = 6;
    vnic::utils::StoreUnaligned(frame.data() + sizeof(ethhdr), ip);

    EXPECT_FALSE(parser.parse(Bytes(frame)).has_value());

    EXPECT_EQ(parser.l2Protocol(), vnic::Parser::L2Protocol::None);
}

TEST(ParserTest, Ipv4WithTooSmallIhlFailsAfterL2IsSet) {
    vnic::Parser parser;
    auto frame = MakeIpv4(4, IPPROTO_UDP, sizeof(udphdr)); // ihl*4 = 16 < 20

    EXPECT_FALSE(parser.parse(Bytes(frame)).has_value());

    EXPECT_EQ(parser.l2Protocol(), vnic::Parser::L2Protocol::Ipv4);
    EXPECT_EQ(parser.l3Protocol(), vnic::Parser::L3Protocol::None);
}

TEST(ParserTest, TruncatedIpv4L4HeaderFails) {
    vnic::Parser parser;
    auto frame = MakeIpv4(5, IPPROTO_TCP, sizeof(tcphdr) - 1);

    EXPECT_FALSE(parser.parse(Bytes(frame)).has_value());

    EXPECT_EQ(parser.l2Protocol(), vnic::Parser::L2Protocol::Ipv4);
    EXPECT_EQ(parser.l3Protocol(), vnic::Parser::L3Protocol::None);
    EXPECT_FALSE(parser.tcp().has_value());
}

TEST(ParserTest, TruncatedIpv6HeaderFails) {
    vnic::Parser parser;
    auto frame = MakeIpv6(IPPROTO_UDP, sizeof(udphdr));

    EXPECT_FALSE(parser.parse(Head(frame, sizeof(ethhdr) + sizeof(ipv6hdr) - 1)).has_value());

    EXPECT_EQ(parser.l2Protocol(), vnic::Parser::L2Protocol::None);
    EXPECT_FALSE(parser.ipv6().has_value());
}

TEST(ParserTest, Ipv6WithBadVersionFails) {
    vnic::Parser parser;
    auto frame  = MakeIpv6(IPPROTO_UDP, sizeof(udphdr));
    auto ip6    = vnic::utils::LoadUnaligned<ipv6hdr>(frame.data() + sizeof(ethhdr));
    ip6.version = 4;
    vnic::utils::StoreUnaligned(frame.data() + sizeof(ethhdr), ip6);

    EXPECT_FALSE(parser.parse(Bytes(frame)).has_value());

    EXPECT_EQ(parser.l2Protocol(), vnic::Parser::L2Protocol::None);
}

TEST(ParserTest, TruncatedIpv6L4HeaderFails) {
    vnic::Parser parser;
    auto frame = MakeIpv6(IPPROTO_UDP, sizeof(udphdr) - 1);

    EXPECT_FALSE(parser.parse(Bytes(frame)).has_value());

    EXPECT_EQ(parser.l2Protocol(), vnic::Parser::L2Protocol::Ipv6);
    EXPECT_EQ(parser.l3Protocol(), vnic::Parser::L3Protocol::None);
    EXPECT_FALSE(parser.udp().has_value());
}

TEST(ParserTest, TruncatedArpHeaderFails) {
    vnic::Parser parser;
    auto frame = MakeArp();

    EXPECT_FALSE(parser.parse(Head(frame, sizeof(ethhdr) + sizeof(arphdr) - 1)).has_value());

    EXPECT_EQ(parser.l2Protocol(), vnic::Parser::L2Protocol::None);
    EXPECT_FALSE(parser.arp().has_value());
}

// ---------------------------------------------------------------------------
// Reuse
// ---------------------------------------------------------------------------

TEST(ParserTest, ReportsFailureOnSecondParseAfterSuccess) {
    vnic::Parser parser;
    auto good = MakeIpv4(5, IPPROTO_UDP, sizeof(udphdr));
    auto bad  = MakeEthernet(0x1234, 16);

    ASSERT_TRUE(parser.parse(Bytes(good)).has_value());
    EXPECT_FALSE(parser.parse(Bytes(bad)).has_value());

    EXPECT_EQ(parser.l2Protocol(), vnic::Parser::L2Protocol::Unknown);
}

TEST(ParserTest, ParseResetsStateFromPreviousFrame) {
    vnic::Parser parser;
    auto setter = MakeIpv4(5, IPPROTO_TCP, sizeof(tcphdr)); // L2=Ipv4, L3=Tcp
    auto arp    = MakeArp();                                // L2=Arp, no L3

    ASSERT_TRUE(parser.parse(Bytes(setter)).has_value());
    ASSERT_EQ(parser.l3Protocol(), vnic::Parser::L3Protocol::Tcp);

    ASSERT_TRUE(parser.parse(Bytes(arp)).has_value());

    EXPECT_EQ(parser.l1Protocol(), vnic::Parser::L1Protocol::Eth);
    EXPECT_EQ(parser.l2Protocol(), vnic::Parser::L2Protocol::Arp);
    EXPECT_EQ(parser.l3Protocol(),
              vnic::Parser::L3Protocol::None); // no stale TCP
    EXPECT_FALSE(parser.tcp().has_value());
    EXPECT_FALSE(parser.udp().has_value());
    EXPECT_FALSE(parser.icmp().has_value());
    EXPECT_FALSE(parser.ipv4().has_value());
}

TEST(ParserTest, ResetClearsStateAfterSuccessfulParse) {
    vnic::Parser parser;
    auto frame = MakeIpv4(5, IPPROTO_UDP, sizeof(udphdr));
    ASSERT_TRUE(parser.parse(Bytes(frame)).has_value());

    parser.reset();

    EXPECT_EQ(parser.l1Protocol(), vnic::Parser::L1Protocol::None);
    EXPECT_EQ(parser.l2Protocol(), vnic::Parser::L2Protocol::None);
    EXPECT_EQ(parser.l3Protocol(), vnic::Parser::L3Protocol::None);
    EXPECT_FALSE(parser.eth().has_value());
    EXPECT_FALSE(parser.ipv4().has_value());
    EXPECT_FALSE(parser.arp().has_value());
    EXPECT_FALSE(parser.udp().has_value());
    EXPECT_EQ(parser.vlanId(), 0u);
    EXPECT_EQ(parser.l2Offset(), 0u);
}

TEST(ParserTest, ResetClearsStateAfterFailedParse) {
    vnic::Parser parser;
    auto bad = MakeEthernet(0x1234, 16); // unsupported EtherType

    ASSERT_FALSE(parser.parse(Bytes(bad)).has_value());
    ASSERT_EQ(parser.l2Protocol(), vnic::Parser::L2Protocol::Unknown);

    parser.reset();

    EXPECT_EQ(parser.l1Protocol(), vnic::Parser::L1Protocol::None);
    EXPECT_EQ(parser.l2Protocol(), vnic::Parser::L2Protocol::None);
    EXPECT_FALSE(parser.eth().has_value());
}

TEST(ParserTest, ResetOnFreshParserIsNoOp) {
    vnic::Parser parser;

    parser.reset();

    EXPECT_EQ(parser.l1Protocol(), vnic::Parser::L1Protocol::None);
    EXPECT_EQ(parser.l2Protocol(), vnic::Parser::L2Protocol::None);
    EXPECT_EQ(parser.l3Protocol(), vnic::Parser::L3Protocol::None);
    EXPECT_FALSE(parser.eth().has_value());
    EXPECT_EQ(parser.l2Offset(), 0u);
}

// ---------------------------------------------------------------------------
// operator<<
// ---------------------------------------------------------------------------

TEST(ParserTest, StreamOperatorPrintsIpv4Tcp) {
    vnic::Parser parser;
    auto frame = MakeIpv4(5, IPPROTO_TCP, sizeof(tcphdr));
    ASSERT_TRUE(parser.parse(Bytes(frame)).has_value());

    std::ostringstream os;
    os << parser;
    const std::string text = os.str();

    EXPECT_NE(text.find("=== Parsed packet ==="), std::string::npos);
    EXPECT_NE(text.find("[L1] Eth"), std::string::npos);
    EXPECT_NE(text.find("[L2] IPv4"), std::string::npos);
    EXPECT_NE(text.find("[L3] TCP"), std::string::npos);
    EXPECT_NE(text.find("eth.dst"), std::string::npos);
    EXPECT_NE(text.find("ip.saddr"), std::string::npos);
}

TEST(ParserTest, StreamOperatorPrintsArp) {
    vnic::Parser parser;
    auto frame = MakeArp();
    ASSERT_TRUE(parser.parse(Bytes(frame)).has_value());

    std::ostringstream os;
    os << parser;
    const std::string text = os.str();

    EXPECT_NE(text.find("[L2] ARP"), std::string::npos);
    EXPECT_NE(text.find("arp.op"), std::string::npos);
    EXPECT_NE(text.find("request"), std::string::npos);
}

TEST(ParserTest, StreamOperatorPrintsVlanAndIpv6) {
    vnic::Parser parser;
    auto frame = MakeVlan(ETH_P_8021Q, ETH_P_IPV6, 42, Ipv6Bytes(IPPROTO_UDP, sizeof(udphdr)));
    ASSERT_TRUE(parser.parse(Bytes(frame)).has_value());

    std::ostringstream os;
    os << parser;
    const std::string text = os.str();

    EXPECT_NE(text.find("[L1] Vlan"), std::string::npos);
    EXPECT_NE(text.find("[L2] IPv6"), std::string::npos);
}

TEST(ParserTest, StreamOperatorPrintsUdp) {
    vnic::Parser parser;
    auto frame = MakeIpv4(5, IPPROTO_UDP, sizeof(udphdr));
    ASSERT_TRUE(parser.parse(Bytes(frame)).has_value());

    std::ostringstream os;
    os << parser;
    const std::string text = os.str();

    EXPECT_NE(text.find("[L3] UDP"), std::string::npos);
    EXPECT_NE(text.find("udp.sport"), std::string::npos);
}

TEST(ParserTest, StreamOperatorPrintsIcmp) {
    vnic::Parser parser;
    auto frame = MakeIpv4(5, IPPROTO_ICMP, sizeof(icmphdr));
    auto icmp  = vnic::utils::LoadUnaligned<icmphdr>(frame.data() + sizeof(ethhdr) + sizeof(iphdr));
    icmp.type  = ICMP_ECHO;
    vnic::utils::StoreUnaligned(frame.data() + sizeof(ethhdr) + sizeof(iphdr), icmp);
    ASSERT_TRUE(parser.parse(Bytes(frame)).has_value());

    std::ostringstream os;
    os << parser;
    const std::string text = os.str();

    EXPECT_NE(text.find("[L3] ICMP"), std::string::npos);
    EXPECT_NE(text.find("echo-request"), std::string::npos);
}

// ===========================================================================
// Text helpers
// ===========================================================================

TEST(ParserTextTest, MacToStringFormatsSixBytes) {
    EXPECT_EQ(vnic::MacToString(kSrcMac), "00:11:22:33:44:55");
    EXPECT_EQ(vnic::MacToString(kDstMac), "66:77:88:99:aa:bb");

    constexpr std::array<std::uint8_t, 6> zeroMac{};
    EXPECT_EQ(vnic::MacToString(zeroMac), "00:00:00:00:00:00");
}

TEST(ParserTextTest, Ipv4ToStringUsesNetworkByteOrder) {
    EXPECT_EQ(vnic::Ipv4ToString(htonl(0xC0A80101u)), "192.168.1.1");
    EXPECT_EQ(vnic::Ipv4ToString(htonl(0x0A000001u)), "10.0.0.1");
    EXPECT_EQ(vnic::Ipv4ToString(0u), "0.0.0.0");
}

TEST(ParserTextTest, Ipv6ToStringUsesCompressedNotation) {
    const std::array<std::uint8_t, 16> address{0x20, 0x01, 0x0d, 0xb8, 0x00, 0x00, 0x00, 0x00,
                                               0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01};
    EXPECT_EQ(vnic::Ipv6ToString(address), "2001:db8::1");

    constexpr std::array<std::uint8_t, 16> loopback{0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1};
    EXPECT_EQ(vnic::Ipv6ToString(loopback), "::1");

    constexpr std::array<std::uint8_t, 16> unspecified{};
    EXPECT_EQ(vnic::Ipv6ToString(unspecified), "::");
}

TEST(ParserTextTest, ProtocolNamesCoverAllValues) {
    EXPECT_EQ(vnic::L1Name(vnic::Parser::L1Protocol::None), "None");
    EXPECT_EQ(vnic::L1Name(vnic::Parser::L1Protocol::Eth), "Eth");
    EXPECT_EQ(vnic::L1Name(vnic::Parser::L1Protocol::Vlan), "Vlan");
    EXPECT_EQ(vnic::L1Name(vnic::Parser::L1Protocol::Unknown), "Unknown");
    EXPECT_EQ(vnic::L1Name(static_cast<vnic::Parser::L1Protocol>(0xFF)), "Unknown");

    EXPECT_EQ(vnic::L2Name(vnic::Parser::L2Protocol::None), "None");
    EXPECT_EQ(vnic::L2Name(vnic::Parser::L2Protocol::Ipv4), "IPv4");
    EXPECT_EQ(vnic::L2Name(vnic::Parser::L2Protocol::Ipv6), "IPv6");
    EXPECT_EQ(vnic::L2Name(vnic::Parser::L2Protocol::Arp), "ARP");
    EXPECT_EQ(vnic::L2Name(vnic::Parser::L2Protocol::Unknown), "Unknown");
    EXPECT_EQ(vnic::L2Name(static_cast<vnic::Parser::L2Protocol>(0xFF)), "Unknown");

    EXPECT_EQ(vnic::L3Name(vnic::Parser::L3Protocol::None), "None");
    EXPECT_EQ(vnic::L3Name(vnic::Parser::L3Protocol::Icmp), "ICMP");
    EXPECT_EQ(vnic::L3Name(vnic::Parser::L3Protocol::Tcp), "TCP");
    EXPECT_EQ(vnic::L3Name(vnic::Parser::L3Protocol::Udp), "UDP");
    EXPECT_EQ(vnic::L3Name(vnic::Parser::L3Protocol::Unknown), "Unknown");
    EXPECT_EQ(vnic::L3Name(static_cast<vnic::Parser::L3Protocol>(0xFF)), "Unknown");
}

TEST(ParserTextTest, ArpOpNameUsesNetworkByteOrder) {
    EXPECT_EQ(vnic::ArpOpName(htons(1)), "request");
    EXPECT_EQ(vnic::ArpOpName(htons(2)), "reply");
    EXPECT_EQ(vnic::ArpOpName(htons(3)), "RARP-request");
    EXPECT_EQ(vnic::ArpOpName(htons(4)), "RARP-reply");
    EXPECT_EQ(vnic::ArpOpName(htons(99)), "?");
}

TEST(ParserTextTest, IcmpTypeNameCoversCommonTypes) {
    EXPECT_EQ(vnic::IcmpTypeName(ICMP_ECHOREPLY), "echo-reply");
    EXPECT_EQ(vnic::IcmpTypeName(ICMP_ECHO), "echo-request");
    EXPECT_EQ(vnic::IcmpTypeName(ICMP_DEST_UNREACH), "dest-unreachable");
    EXPECT_EQ(vnic::IcmpTypeName(ICMP_TIME_EXCEEDED), "time-exceeded");
    EXPECT_EQ(vnic::IcmpTypeName(ICMP_REDIRECT), "redirect");
    EXPECT_EQ(vnic::IcmpTypeName(static_cast<std::uint8_t>(200)), "?");
}
