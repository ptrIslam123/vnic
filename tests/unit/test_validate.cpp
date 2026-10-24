#include <gtest/gtest.h>

#include "parser/parser.h"
#include "validate/validate.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include <arpa/inet.h>
#include <linux/if_ether.h>
#include <linux/ip.h>
#include <net/if_arp.h>

namespace {

constexpr std::array<std::uint8_t, 6> kPortMac{0x00, 0x11, 0x22, 0x33, 0x44, 0x55};
constexpr std::array<std::uint8_t, 6> kForeignMac{0x02, 0x02, 0x02, 0x02, 0x02, 0x02};
constexpr std::array<std::uint8_t, 6> kBroadcastMac{0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
constexpr std::array<std::uint8_t, 6> kMulticastMac{0x01, 0x00, 0x5E, 0x00, 0x00, 0x01};

std::span<const std::byte> Bytes(const std::vector<std::byte>& frame) noexcept {
    return std::span<const std::byte>{frame.data(), frame.size()};
}

void SetIpv4Checksum(std::byte* ip, bool valid) {
    auto* bytes = reinterpret_cast<std::uint8_t*>(ip);
    bytes[10]   = 0;
    bytes[11]   = 0;

    std::uint32_t sum{0};
    for (std::size_t i{0}; i + 1 < sizeof(iphdr); i += 2) {
        sum += (static_cast<std::uint32_t>(bytes[i]) << 8) | bytes[i + 1];
    }
    while ((sum >> 16) != 0) {
        sum = (sum & 0xFFFFU) + (sum >> 16);
    }

    auto checksum = static_cast<std::uint16_t>(~sum);
    if (!valid) {
        checksum ^= 0xFFFF;
    }
    bytes[10] = static_cast<std::uint8_t>(checksum >> 8);
    bytes[11] = static_cast<std::uint8_t>(checksum & 0xFF);
}

std::vector<std::byte> MakeIpv4Frame(std::size_t frameSize, std::uint16_t totalLength, std::uint8_t protocol,
                                     bool validChecksum = true, const std::array<std::uint8_t, 6>& dstMac = kPortMac,
                                     std::optional<std::uint16_t> vlan = std::nullopt) {
    const std::size_t l2Offset{sizeof(ethhdr) + (vlan.has_value() ? 4 : 0)};
    frameSize = std::max(frameSize, l2Offset + sizeof(iphdr));

    std::vector<std::byte> frame(frameSize, std::byte{0});

    ethhdr eth{};
    std::memcpy(eth.h_dest, dstMac.data(), dstMac.size());
    std::memcpy(eth.h_source, kPortMac.data(), kPortMac.size());
    eth.h_proto = htons(vlan.has_value() ? ETH_P_8021Q : ETH_P_IP);
    std::memcpy(frame.data(), &eth, sizeof(eth));

    if (vlan.has_value()) {
        const std::uint16_t tci{htons(*vlan)};
        const std::uint16_t innerType{htons(ETH_P_IP)};
        std::memcpy(frame.data() + sizeof(ethhdr), &tci, sizeof(tci));
        std::memcpy(frame.data() + sizeof(ethhdr) + sizeof(tci), &innerType, sizeof(innerType));
    }

    iphdr ip{};
    ip.version  = 4;
    ip.ihl      = 5;
    ip.ttl      = 64;
    ip.protocol = protocol;
    ip.tot_len  = htons(totalLength);
    ip.saddr    = htonl(0x0A000001);
    ip.daddr    = htonl(0x0A000002);
    std::memcpy(frame.data() + l2Offset, &ip, sizeof(ip));
    SetIpv4Checksum(frame.data() + l2Offset, validChecksum);

    return frame;
}

std::vector<std::byte> MakeArpFrame(std::size_t frameSize = 64) {
    frameSize = std::max(frameSize, sizeof(ethhdr) + sizeof(arphdr) + 20);

    std::vector<std::byte> frame(frameSize, std::byte{0});

    ethhdr eth{};
    std::memcpy(eth.h_dest, kBroadcastMac.data(), kBroadcastMac.size());
    std::memcpy(eth.h_source, kPortMac.data(), kPortMac.size());
    eth.h_proto = htons(ETH_P_ARP);
    std::memcpy(frame.data(), &eth, sizeof(eth));

    arphdr arp{};
    arp.ar_hrd = htons(ARPHRD_ETHER);
    arp.ar_pro = htons(ETH_P_IP);
    arp.ar_hln = 6;
    arp.ar_pln = 4;
    arp.ar_op  = htons(1);
    std::memcpy(frame.data() + sizeof(ethhdr), &arp, sizeof(arp));

    return frame;
}

vnic::Parser ParseFrame(const std::vector<std::byte>& frame) {
    vnic::Parser parser;
    EXPECT_TRUE(parser.parse(Bytes(frame)).has_value());
    return parser;
}

} // namespace

TEST(ValidateTest, ParseMacAcceptsValidAndRejectsMalformedAddresses) {
    const auto mac = vnic::validate::ParseMac("00:11:22:33:44:55");
    ASSERT_TRUE(mac.has_value());
    EXPECT_EQ(*mac, kPortMac);

    const auto uppercase = vnic::validate::ParseMac("AA:BB:CC:DD:EE:FF");
    ASSERT_TRUE(uppercase.has_value());
    EXPECT_EQ(*uppercase, (std::array<std::uint8_t, 6>{0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF}));

    EXPECT_FALSE(vnic::validate::ParseMac("").has_value());
    EXPECT_FALSE(vnic::validate::ParseMac("00:11:22:33:44").has_value());
    EXPECT_FALSE(vnic::validate::ParseMac("00:11:22:33:44:100").has_value());
    EXPECT_FALSE(vnic::validate::ParseMac("00:11:22:33:44:gg").has_value());
}

TEST(ValidateTest, CheckFrameSizeEnforcesMinimumAndMtuBoundaries) {
    constexpr std::size_t kMinFrameSize{60};
    constexpr std::uint32_t kMtu{1500};

    const auto runt      = MakeIpv4Frame(59, 20, 0xFE);
    const auto minimum   = MakeIpv4Frame(60, 20, 0xFE);
    const auto maximum   = MakeIpv4Frame(1514, 20, 0xFE);
    const auto oversized = MakeIpv4Frame(1515, 20, 0xFE);

    const auto runtParser      = ParseFrame(runt);
    const auto minimumParser   = ParseFrame(minimum);
    const auto maximumParser   = ParseFrame(maximum);
    const auto oversizedParser = ParseFrame(oversized);

    EXPECT_FALSE(vnic::validate::CheckFrameSize(Bytes(runt), runtParser, kMinFrameSize, kMtu));
    EXPECT_TRUE(vnic::validate::CheckFrameSize(Bytes(minimum), minimumParser, kMinFrameSize, kMtu));
    EXPECT_TRUE(vnic::validate::CheckFrameSize(Bytes(maximum), maximumParser, kMinFrameSize, kMtu));
    EXPECT_FALSE(vnic::validate::CheckFrameSize(Bytes(oversized), oversizedParser, kMinFrameSize, kMtu));
}

TEST(ValidateTest, CheckFrameSizeUsesVlanAdjustedL2Offset) {
    constexpr std::size_t kMinFrameSize{60};
    constexpr std::uint32_t kMtu{1500};

    const auto maximum   = MakeIpv4Frame(1518, 20, 0xFE, true, kPortMac, 10);
    const auto oversized = MakeIpv4Frame(1519, 20, 0xFE, true, kPortMac, 10);

    const auto maximumParser   = ParseFrame(maximum);
    const auto oversizedParser = ParseFrame(oversized);

    EXPECT_EQ(maximumParser.l2Offset(), sizeof(ethhdr) + 4);
    EXPECT_TRUE(vnic::validate::CheckFrameSize(Bytes(maximum), maximumParser, kMinFrameSize, kMtu));
    EXPECT_FALSE(vnic::validate::CheckFrameSize(Bytes(oversized), oversizedParser, kMinFrameSize, kMtu));
}

TEST(ValidateTest, CheckFcsAlwaysAcceptsInCurrentArchitecture) {
    const std::array<std::byte, 4> fcs{std::byte{0x12}, std::byte{0x34}, std::byte{0x56}, std::byte{0x78}};

    EXPECT_TRUE(vnic::validate::CheckFcs({}));
    EXPECT_TRUE(vnic::validate::CheckFcs(fcs));
}

TEST(ValidateTest, CheckDstMacAcceptsOwnBroadcastAndMulticastAddresses) {
    const auto ownFrame       = MakeIpv4Frame(64, 20, 0xFE, true, kPortMac);
    const auto foreignFrame   = MakeIpv4Frame(64, 20, 0xFE, true, kForeignMac);
    const auto broadcastFrame = MakeIpv4Frame(64, 20, 0xFE, true, kBroadcastMac);
    const auto multicastFrame = MakeIpv4Frame(64, 20, 0xFE, true, kMulticastMac);

    const auto ownParser       = ParseFrame(ownFrame);
    const auto foreignParser   = ParseFrame(foreignFrame);
    const auto broadcastParser = ParseFrame(broadcastFrame);
    const auto multicastParser = ParseFrame(multicastFrame);

    EXPECT_TRUE(vnic::validate::CheckDstMac(ownParser, kPortMac, false));
    EXPECT_FALSE(vnic::validate::CheckDstMac(foreignParser, kPortMac, false));
    EXPECT_TRUE(vnic::validate::CheckDstMac(broadcastParser, kPortMac, false));
    EXPECT_TRUE(vnic::validate::CheckDstMac(multicastParser, kPortMac, false));
    EXPECT_TRUE(vnic::validate::CheckDstMac(foreignParser, kPortMac, true));
}

TEST(ValidateTest, CheckDstMacAcceptsWhenEthernetHeaderIsAbsent) {
    vnic::Parser parser;
    EXPECT_TRUE(vnic::validate::CheckDstMac(parser, kPortMac, false));
}

TEST(ValidateTest, CheckVlanFilterHandlesUntaggedAndAllowedVlans) {
    const auto plainFrame  = MakeIpv4Frame(64, 20, 0xFE);
    const auto taggedFrame = MakeIpv4Frame(64, 20, 0xFE, true, kPortMac, 10);

    const auto plainParser  = ParseFrame(plainFrame);
    const auto taggedParser = ParseFrame(taggedFrame);

    EXPECT_TRUE(vnic::validate::CheckVlanFilter(plainParser, {10, 20}));
    EXPECT_TRUE(vnic::validate::CheckVlanFilter(taggedParser, {}));
    EXPECT_TRUE(vnic::validate::CheckVlanFilter(taggedParser, {10, 20}));
    EXPECT_FALSE(vnic::validate::CheckVlanFilter(taggedParser, {20}));
}

TEST(ValidateTest, CheckIpTotalLengthRejectsShortAndTruncatedLengths) {
    const auto valid       = MakeIpv4Frame(64, 50, 0xFE);
    const auto shortHeader = MakeIpv4Frame(64, 19, 0xFE);
    const auto truncated   = MakeIpv4Frame(64, 100, 0xFE);
    const auto exact       = MakeIpv4Frame(34, 20, 0xFE);
    const auto arp         = MakeArpFrame();

    const auto validParser       = ParseFrame(valid);
    const auto shortHeaderParser = ParseFrame(shortHeader);
    const auto truncatedParser   = ParseFrame(truncated);
    const auto exactParser       = ParseFrame(exact);
    const auto arpParser         = ParseFrame(arp);

    EXPECT_TRUE(vnic::validate::CheckIpTotalLength(Bytes(valid), validParser));
    EXPECT_FALSE(vnic::validate::CheckIpTotalLength(Bytes(shortHeader), shortHeaderParser));
    EXPECT_FALSE(vnic::validate::CheckIpTotalLength(Bytes(truncated), truncatedParser));
    EXPECT_TRUE(vnic::validate::CheckIpTotalLength(Bytes(exact), exactParser));
    EXPECT_TRUE(vnic::validate::CheckIpTotalLength(Bytes(arp), arpParser));

    vnic::Parser empty;
    EXPECT_TRUE(vnic::validate::CheckIpTotalLength({}, empty));
}

TEST(ValidateTest, CheckIpv4ChecksumRejectsCorruptedHeaderAndSkipsNonIpv4) {
    const auto valid   = MakeIpv4Frame(64, 50, 0xFE, true);
    const auto invalid = MakeIpv4Frame(64, 50, 0xFE, false);
    const auto arp     = MakeArpFrame();

    const auto validParser   = ParseFrame(valid);
    const auto invalidParser = ParseFrame(invalid);
    const auto arpParser     = ParseFrame(arp);

    EXPECT_TRUE(vnic::validate::CheckIpv4Checksum(Bytes(valid), validParser));
    EXPECT_FALSE(vnic::validate::CheckIpv4Checksum(Bytes(invalid), invalidParser));
    EXPECT_TRUE(vnic::validate::CheckIpv4Checksum(Bytes(arp), arpParser));

    vnic::Parser empty;
    EXPECT_TRUE(vnic::validate::CheckIpv4Checksum({}, empty));
}
