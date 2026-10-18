#include <gtest/gtest.h>

#include "parser/parser.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>
#include <sstream>
#include <string>
#include <vector>

#include <arpa/inet.h>
#include <linux/if_ether.h>
#include <linux/ipv6.h>
#include <net/if_arp.h>
#include <netinet/ip.h>
#include <netinet/ip_icmp.h>
#include <netinet/tcp.h>
#include <netinet/udp.h>

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

} // namespace

// ---------------------------------------------------------------------------
// Default (unparsed) state
// ---------------------------------------------------------------------------

TEST(ParserTest, DefaultStateIsEmpty) {
    vnic::Parser parser;

    EXPECT_EQ(parser.l1Protocol(), vnic::Parser::L1Protocol::None);
    EXPECT_EQ(parser.l2Protocol(), vnic::Parser::L2Protocol::None);
    EXPECT_EQ(parser.l3Protocol(), vnic::Parser::L3Protocol::None);

    EXPECT_EQ(parser.eth(), nullptr);
    EXPECT_EQ(parser.ipv4(), nullptr);
    EXPECT_EQ(parser.ipv6(), nullptr);
    EXPECT_EQ(parser.arp(), nullptr);
    EXPECT_EQ(parser.icmp(), nullptr);
    EXPECT_EQ(parser.tcp(), nullptr);
    EXPECT_EQ(parser.udp(), nullptr);

    EXPECT_EQ(parser.vlanId(), 0u);
    EXPECT_EQ(parser.l2Offset(), 0u);
}

// ---------------------------------------------------------------------------
// L1: Ethernet / VLAN
// ---------------------------------------------------------------------------

TEST(ParserTest, ParsesPlainEthernetIpv4) {
    vnic::Parser parser;
    auto frame = MakeIpv4(5, IPPROTO_UDP, sizeof(udphdr));

    ASSERT_TRUE(parser.parse(Bytes(frame)));

    EXPECT_EQ(parser.l1Protocol(), vnic::Parser::L1Protocol::Eth);
    EXPECT_EQ(parser.eth(), reinterpret_cast<const ethhdr*>(Base(frame)));
    EXPECT_EQ(parser.vlanId(), 0u);
    EXPECT_EQ(parser.l2Offset(), sizeof(ethhdr));
}

TEST(ParserTest, ParsesVlanTaggedIpv4) {
    vnic::Parser parser;
    auto frame = MakeVlan(ETH_P_8021Q, ETH_P_IP, 0x0ABC, Ipv4Bytes(5, IPPROTO_UDP, sizeof(udphdr)));

    ASSERT_TRUE(parser.parse(Bytes(frame)));

    EXPECT_EQ(parser.l1Protocol(), vnic::Parser::L1Protocol::Vlan);
    EXPECT_EQ(parser.eth(), reinterpret_cast<const ethhdr*>(Base(frame)));
    EXPECT_EQ(parser.vlanId(), 0x0ABCu);
    EXPECT_EQ(parser.l2Offset(), sizeof(ethhdr) + 4);
}

TEST(ParserTest, ParsesVlanTaggedWithQinQ) {
    vnic::Parser parser;
    auto frame = MakeVlan(ETH_P_8021AD, ETH_P_IP, 0x0BCD, Ipv4Bytes(5, IPPROTO_UDP, sizeof(udphdr)));

    ASSERT_TRUE(parser.parse(Bytes(frame)));

    EXPECT_EQ(parser.l1Protocol(), vnic::Parser::L1Protocol::Vlan);
    EXPECT_EQ(parser.vlanId(), 0x0BCDu);
}

TEST(ParserTest, VlanIdIsMaskedToTwelveBits) {
    vnic::Parser parser;
    // 0xF000 sets the priority and DEI bits; only the low 12 bits are the VLAN ID.
    auto frame = MakeVlan(ETH_P_8021Q, ETH_P_IP, 0xF123, Ipv4Bytes(5, IPPROTO_UDP, sizeof(udphdr)));

    ASSERT_TRUE(parser.parse(Bytes(frame)));

    EXPECT_EQ(parser.vlanId(), 0x0123u);
}

TEST(ParserTest, ParsesVlanTaggedIpv6) {
    vnic::Parser parser;
    auto frame = MakeVlan(ETH_P_8021Q, ETH_P_IPV6, 100, Ipv6Bytes(IPPROTO_UDP, sizeof(udphdr)));

    ASSERT_TRUE(parser.parse(Bytes(frame)));

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
    ASSERT_TRUE(parser.parse(Bytes(frame)));

    EXPECT_EQ(parser.l1Protocol(), vnic::Parser::L1Protocol::Vlan);
    EXPECT_EQ(parser.l2Protocol(), vnic::Parser::L2Protocol::Arp);
    EXPECT_EQ(parser.vlanId(), 7u);
    EXPECT_EQ(parser.arp(), reinterpret_cast<const arphdr*>(frame.data() + sizeof(ethhdr) + 4));
}

TEST(ParserTest, UnknownEtherTypeFailsWithUnknownL2) {
    vnic::Parser parser;
    auto frame = MakeEthernet(0x1234, 32);

    EXPECT_FALSE(parser.parse(Bytes(frame)));

    EXPECT_EQ(parser.l1Protocol(), vnic::Parser::L1Protocol::Eth);
    EXPECT_EQ(parser.eth(), reinterpret_cast<const ethhdr*>(Base(frame)));
    EXPECT_EQ(parser.l2Protocol(), vnic::Parser::L2Protocol::Unknown);
    EXPECT_EQ(parser.ipv4(), nullptr);
    EXPECT_EQ(parser.ipv6(), nullptr);
    EXPECT_EQ(parser.arp(), nullptr);
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

    ASSERT_TRUE(parser.parse(Bytes(frame)));

    EXPECT_EQ(parser.l2Protocol(), vnic::Parser::L2Protocol::Ipv4);
    EXPECT_EQ(parser.l3Protocol(), vnic::Parser::L3Protocol::Udp);
    EXPECT_EQ(parser.ipv4(), reinterpret_cast<const iphdr*>(frame.data() + sizeof(ethhdr)));
    ASSERT_NE(parser.udp(), nullptr);
    EXPECT_EQ(parser.udp(), reinterpret_cast<const udphdr*>(frame.data() + sizeof(ethhdr) + sizeof(iphdr)));
    EXPECT_EQ(parser.tcp(), nullptr);
    EXPECT_EQ(parser.icmp(), nullptr);
    EXPECT_EQ(parser.ipv6(), nullptr);
    EXPECT_EQ(parser.arp(), nullptr);
}

TEST(ParserTest, ParsesIpv4Tcp) {
    vnic::Parser parser;
    auto frame = MakeIpv4(5, IPPROTO_TCP, sizeof(tcphdr));

    ASSERT_TRUE(parser.parse(Bytes(frame)));

    EXPECT_EQ(parser.l3Protocol(), vnic::Parser::L3Protocol::Tcp);
    EXPECT_EQ(parser.tcp(), reinterpret_cast<const tcphdr*>(frame.data() + sizeof(ethhdr) + sizeof(iphdr)));
    EXPECT_EQ(parser.udp(), nullptr);
    EXPECT_EQ(parser.icmp(), nullptr);
}

TEST(ParserTest, ParsesIpv4Icmp) {
    vnic::Parser parser;
    auto frame = MakeIpv4(5, IPPROTO_ICMP, sizeof(icmphdr));
    auto* icmp = reinterpret_cast<icmphdr*>(frame.data() + sizeof(ethhdr) + sizeof(iphdr));
    icmp->type = ICMP_ECHO;
    icmp->code = 0;

    ASSERT_TRUE(parser.parse(Bytes(frame)));

    EXPECT_EQ(parser.l3Protocol(), vnic::Parser::L3Protocol::Icmp);
    ASSERT_NE(parser.icmp(), nullptr);
    EXPECT_EQ(parser.icmp(), reinterpret_cast<const icmphdr*>(frame.data() + sizeof(ethhdr) + sizeof(iphdr)));
    EXPECT_EQ(parser.tcp(), nullptr);
    EXPECT_EQ(parser.udp(), nullptr);
}

TEST(ParserTest, IpV4UnknownL4ProtocolIsReportedAsUnknown) {
    vnic::Parser parser;
    auto frame = MakeIpv4(5, 99, 16); // 99 is not TCP/UDP/ICMP

    ASSERT_TRUE(parser.parse(Bytes(frame)));

    EXPECT_EQ(parser.l2Protocol(), vnic::Parser::L2Protocol::Ipv4);
    EXPECT_EQ(parser.l3Protocol(), vnic::Parser::L3Protocol::Unknown);
    EXPECT_EQ(parser.tcp(), nullptr);
    EXPECT_EQ(parser.udp(), nullptr);
    EXPECT_EQ(parser.icmp(), nullptr);
}

TEST(ParserTest, Ipv4OptionsAreSkippedUsingIhl) {
    vnic::Parser parser;
    auto frame = MakeIpv4(6, IPPROTO_UDP, sizeof(udphdr)); // 24-byte IP header

    ASSERT_TRUE(parser.parse(Bytes(frame)));

    EXPECT_EQ(parser.l3Protocol(), vnic::Parser::L3Protocol::Udp);
    EXPECT_EQ(parser.udp(), reinterpret_cast<const udphdr*>(frame.data() + sizeof(ethhdr) + 24));
}

// ---------------------------------------------------------------------------
// L2 / L3: IPv6
// ---------------------------------------------------------------------------

TEST(ParserTest, ParsesIpv6Udp) {
    vnic::Parser parser;
    auto frame = MakeIpv6(IPPROTO_UDP, sizeof(udphdr));

    ASSERT_TRUE(parser.parse(Bytes(frame)));

    EXPECT_EQ(parser.l2Protocol(), vnic::Parser::L2Protocol::Ipv6);
    EXPECT_EQ(parser.l3Protocol(), vnic::Parser::L3Protocol::Udp);
    EXPECT_EQ(parser.ipv6(), reinterpret_cast<const ipv6hdr*>(frame.data() + sizeof(ethhdr)));
    EXPECT_EQ(parser.udp(), reinterpret_cast<const udphdr*>(frame.data() + sizeof(ethhdr) + sizeof(ipv6hdr)));
    EXPECT_EQ(parser.ipv4(), nullptr);
    EXPECT_EQ(parser.arp(), nullptr);
}

TEST(ParserTest, ParsesIpv6Tcp) {
    vnic::Parser parser;
    auto frame = MakeIpv6(IPPROTO_TCP, sizeof(tcphdr));

    ASSERT_TRUE(parser.parse(Bytes(frame)));

    EXPECT_EQ(parser.l3Protocol(), vnic::Parser::L3Protocol::Tcp);
    EXPECT_EQ(parser.tcp(), reinterpret_cast<const tcphdr*>(frame.data() + sizeof(ethhdr) + sizeof(ipv6hdr)));
}

TEST(ParserTest, ParsesIpv6Icmp) {
    vnic::Parser parser;
    auto frame = MakeIpv6(IPPROTO_ICMPV6, sizeof(icmphdr));

    ASSERT_TRUE(parser.parse(Bytes(frame)));

    EXPECT_EQ(parser.l3Protocol(), vnic::Parser::L3Protocol::Icmp);
    ASSERT_NE(parser.icmp(), nullptr);
}

TEST(ParserTest, Ipv6UnknownNextHeaderIsReportedAsUnknown) {
    vnic::Parser parser;
    auto frame = MakeIpv6(99, 16);

    ASSERT_TRUE(parser.parse(Bytes(frame)));

    EXPECT_EQ(parser.l2Protocol(), vnic::Parser::L2Protocol::Ipv6);
    EXPECT_EQ(parser.l3Protocol(), vnic::Parser::L3Protocol::Unknown);
    EXPECT_EQ(parser.tcp(), nullptr);
    EXPECT_EQ(parser.udp(), nullptr);
    EXPECT_EQ(parser.icmp(), nullptr);
}

// ---------------------------------------------------------------------------
// L2: ARP
// ---------------------------------------------------------------------------

TEST(ParserTest, ParsesArp) {
    vnic::Parser parser;
    auto frame = MakeArp();

    ASSERT_TRUE(parser.parse(Bytes(frame)));

    EXPECT_EQ(parser.l1Protocol(), vnic::Parser::L1Protocol::Eth);
    EXPECT_EQ(parser.l2Protocol(), vnic::Parser::L2Protocol::Arp);
    EXPECT_EQ(parser.arp(), reinterpret_cast<const arphdr*>(frame.data() + sizeof(ethhdr)));
    EXPECT_EQ(parser.ipv4(), nullptr);
    EXPECT_EQ(parser.ipv6(), nullptr);
    EXPECT_EQ(parser.l3Protocol(), vnic::Parser::L3Protocol::None);
}

// ---------------------------------------------------------------------------
// Malformed / truncated input
// ---------------------------------------------------------------------------

TEST(ParserTest, EmptyBufferFailsWithoutStateChange) {
    vnic::Parser parser;
    std::vector<std::byte> empty;

    EXPECT_FALSE(parser.parse(Bytes(empty)));

    EXPECT_EQ(parser.l1Protocol(), vnic::Parser::L1Protocol::None);
    EXPECT_EQ(parser.eth(), nullptr);
    EXPECT_EQ(parser.l2Offset(), 0u);
}

TEST(ParserTest, TruncatedEthernetHeaderFails) {
    vnic::Parser parser;
    auto frame = MakeEthernet(ETH_P_IP, 64);

    EXPECT_FALSE(parser.parse(Head(frame, sizeof(ethhdr) - 1)));

    EXPECT_EQ(parser.l1Protocol(), vnic::Parser::L1Protocol::None);
    EXPECT_EQ(parser.eth(), nullptr);
}

TEST(ParserTest, TruncatedVlanTagFails) {
    vnic::Parser parser;
    auto frame = MakeVlan(ETH_P_8021Q, ETH_P_IP, 5, Ipv4Bytes(5, IPPROTO_UDP, sizeof(udphdr)));

    // Only 3 of the 4 required VLAN bytes are present.
    EXPECT_FALSE(parser.parse(Head(frame, sizeof(ethhdr) + 3)));

    EXPECT_EQ(parser.l1Protocol(), vnic::Parser::L1Protocol::Eth);
    EXPECT_EQ(parser.vlanId(), 0u);
}

TEST(ParserTest, TruncatedIpv4HeaderFails) {
    vnic::Parser parser;
    auto frame = MakeIpv4(5, IPPROTO_UDP, sizeof(udphdr));

    EXPECT_FALSE(parser.parse(Head(frame, sizeof(ethhdr) + sizeof(iphdr) - 1)));

    EXPECT_EQ(parser.l1Protocol(), vnic::Parser::L1Protocol::Eth);
    EXPECT_EQ(parser.l2Protocol(), vnic::Parser::L2Protocol::None);
    EXPECT_EQ(parser.ipv4(), nullptr);
}

TEST(ParserTest, Ipv4WithBadVersionFails) {
    vnic::Parser parser;
    auto frame  = MakeIpv4(5, IPPROTO_UDP, sizeof(udphdr));
    auto* ip    = reinterpret_cast<iphdr*>(frame.data() + sizeof(ethhdr));
    ip->version = 6;

    EXPECT_FALSE(parser.parse(Bytes(frame)));

    EXPECT_EQ(parser.l2Protocol(), vnic::Parser::L2Protocol::None);
}

TEST(ParserTest, Ipv4WithTooSmallIhlFailsAfterL2IsSet) {
    vnic::Parser parser;
    auto frame = MakeIpv4(4, IPPROTO_UDP, sizeof(udphdr)); // ihl*4 = 16 < 20

    EXPECT_FALSE(parser.parse(Bytes(frame)));

    EXPECT_EQ(parser.l2Protocol(), vnic::Parser::L2Protocol::Ipv4);
    EXPECT_EQ(parser.l3Protocol(), vnic::Parser::L3Protocol::None);
}

TEST(ParserTest, TruncatedIpv4L4HeaderFails) {
    vnic::Parser parser;
    auto frame = MakeIpv4(5, IPPROTO_TCP, sizeof(tcphdr) - 1);

    EXPECT_FALSE(parser.parse(Bytes(frame)));

    EXPECT_EQ(parser.l2Protocol(), vnic::Parser::L2Protocol::Ipv4);
    EXPECT_EQ(parser.l3Protocol(), vnic::Parser::L3Protocol::None);
    EXPECT_EQ(parser.tcp(), nullptr);
}

TEST(ParserTest, TruncatedIpv6HeaderFails) {
    vnic::Parser parser;
    auto frame = MakeIpv6(IPPROTO_UDP, sizeof(udphdr));

    EXPECT_FALSE(parser.parse(Head(frame, sizeof(ethhdr) + sizeof(ipv6hdr) - 1)));

    EXPECT_EQ(parser.l2Protocol(), vnic::Parser::L2Protocol::None);
    EXPECT_EQ(parser.ipv6(), nullptr);
}

TEST(ParserTest, Ipv6WithBadVersionFails) {
    vnic::Parser parser;
    auto frame   = MakeIpv6(IPPROTO_UDP, sizeof(udphdr));
    auto* ip6    = reinterpret_cast<ipv6hdr*>(frame.data() + sizeof(ethhdr));
    ip6->version = 4;

    EXPECT_FALSE(parser.parse(Bytes(frame)));

    EXPECT_EQ(parser.l2Protocol(), vnic::Parser::L2Protocol::None);
}

TEST(ParserTest, TruncatedIpv6L4HeaderFails) {
    vnic::Parser parser;
    auto frame = MakeIpv6(IPPROTO_UDP, sizeof(udphdr) - 1);

    EXPECT_FALSE(parser.parse(Bytes(frame)));

    EXPECT_EQ(parser.l2Protocol(), vnic::Parser::L2Protocol::Ipv6);
    EXPECT_EQ(parser.l3Protocol(), vnic::Parser::L3Protocol::None);
    EXPECT_EQ(parser.udp(), nullptr);
}

TEST(ParserTest, TruncatedArpHeaderFails) {
    vnic::Parser parser;
    auto frame = MakeArp();

    EXPECT_FALSE(parser.parse(Head(frame, sizeof(ethhdr) + sizeof(arphdr) - 1)));

    EXPECT_EQ(parser.l2Protocol(), vnic::Parser::L2Protocol::None);
    EXPECT_EQ(parser.arp(), nullptr);
}

// ---------------------------------------------------------------------------
// Reuse
// ---------------------------------------------------------------------------

TEST(ParserTest, ReportsFailureOnSecondParseAfterSuccess) {
    vnic::Parser parser;
    auto good = MakeIpv4(5, IPPROTO_UDP, sizeof(udphdr));
    auto bad  = MakeEthernet(0x1234, 16);

    ASSERT_TRUE(parser.parse(Bytes(good)));
    EXPECT_FALSE(parser.parse(Bytes(bad)));

    EXPECT_EQ(parser.l2Protocol(), vnic::Parser::L2Protocol::Unknown);
}

TEST(ParserTest, ParseResetsStateFromPreviousFrame) {
    vnic::Parser parser;
    auto setter = MakeIpv4(5, IPPROTO_TCP, sizeof(tcphdr)); // L2=Ipv4, L3=Tcp
    auto arp    = MakeArp();                                // L2=Arp, no L3

    ASSERT_TRUE(parser.parse(Bytes(setter)));
    ASSERT_EQ(parser.l3Protocol(), vnic::Parser::L3Protocol::Tcp);

    ASSERT_TRUE(parser.parse(Bytes(arp)));

    EXPECT_EQ(parser.l1Protocol(), vnic::Parser::L1Protocol::Eth);
    EXPECT_EQ(parser.l2Protocol(), vnic::Parser::L2Protocol::Arp);
    EXPECT_EQ(parser.l3Protocol(), vnic::Parser::L3Protocol::None); // no stale TCP
    EXPECT_EQ(parser.tcp(), nullptr);
    EXPECT_EQ(parser.udp(), nullptr);
    EXPECT_EQ(parser.icmp(), nullptr);
    EXPECT_EQ(parser.ipv4(), nullptr);
}

TEST(ParserTest, ResetClearsStateAfterSuccessfulParse) {
    vnic::Parser parser;
    auto frame = MakeIpv4(5, IPPROTO_UDP, sizeof(udphdr));
    ASSERT_TRUE(parser.parse(Bytes(frame)));

    parser.reset();

    EXPECT_EQ(parser.l1Protocol(), vnic::Parser::L1Protocol::None);
    EXPECT_EQ(parser.l2Protocol(), vnic::Parser::L2Protocol::None);
    EXPECT_EQ(parser.l3Protocol(), vnic::Parser::L3Protocol::None);
    EXPECT_EQ(parser.eth(), nullptr);
    EXPECT_EQ(parser.ipv4(), nullptr);
    EXPECT_EQ(parser.arp(), nullptr);
    EXPECT_EQ(parser.udp(), nullptr);
    EXPECT_EQ(parser.vlanId(), 0u);
    EXPECT_EQ(parser.l2Offset(), 0u);
}

TEST(ParserTest, ResetClearsStateAfterFailedParse) {
    vnic::Parser parser;
    auto bad = MakeEthernet(0x1234, 16); // unsupported EtherType

    ASSERT_FALSE(parser.parse(Bytes(bad)));
    ASSERT_EQ(parser.l2Protocol(), vnic::Parser::L2Protocol::Unknown);

    parser.reset();

    EXPECT_EQ(parser.l1Protocol(), vnic::Parser::L1Protocol::None);
    EXPECT_EQ(parser.l2Protocol(), vnic::Parser::L2Protocol::None);
    EXPECT_EQ(parser.eth(), nullptr);
}

TEST(ParserTest, ResetOnFreshParserIsNoOp) {
    vnic::Parser parser;

    parser.reset();

    EXPECT_EQ(parser.l1Protocol(), vnic::Parser::L1Protocol::None);
    EXPECT_EQ(parser.l2Protocol(), vnic::Parser::L2Protocol::None);
    EXPECT_EQ(parser.l3Protocol(), vnic::Parser::L3Protocol::None);
    EXPECT_EQ(parser.eth(), nullptr);
    EXPECT_EQ(parser.l2Offset(), 0u);
}

// ---------------------------------------------------------------------------
// operator<<
// ---------------------------------------------------------------------------

TEST(ParserTest, StreamOperatorPrintsIpv4Tcp) {
    vnic::Parser parser;
    auto frame = MakeIpv4(5, IPPROTO_TCP, sizeof(tcphdr));
    ASSERT_TRUE(parser.parse(Bytes(frame)));

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
    ASSERT_TRUE(parser.parse(Bytes(frame)));

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
    ASSERT_TRUE(parser.parse(Bytes(frame)));

    std::ostringstream os;
    os << parser;
    const std::string text = os.str();

    EXPECT_NE(text.find("[L1] Vlan"), std::string::npos);
    EXPECT_NE(text.find("[L2] IPv6"), std::string::npos);
}

TEST(ParserTest, StreamOperatorPrintsUdp) {
    vnic::Parser parser;
    auto frame = MakeIpv4(5, IPPROTO_UDP, sizeof(udphdr));
    ASSERT_TRUE(parser.parse(Bytes(frame)));

    std::ostringstream os;
    os << parser;
    const std::string text = os.str();

    EXPECT_NE(text.find("[L3] UDP"), std::string::npos);
    EXPECT_NE(text.find("udp.sport"), std::string::npos);
}

TEST(ParserTest, StreamOperatorPrintsIcmp) {
    vnic::Parser parser;
    auto frame = MakeIpv4(5, IPPROTO_ICMP, sizeof(icmphdr));
    auto* icmp = reinterpret_cast<icmphdr*>(frame.data() + sizeof(ethhdr) + sizeof(iphdr));
    icmp->type = ICMP_ECHO;
    ASSERT_TRUE(parser.parse(Bytes(frame)));

    std::ostringstream os;
    os << parser;
    const std::string text = os.str();

    EXPECT_NE(text.find("[L3] ICMP"), std::string::npos);
    EXPECT_NE(text.find("echo-request"), std::string::npos);
}
