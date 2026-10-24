#include "parser/parser.h"
#include "utils/unaligned.h"

#include <array>
#include <iomanip>
#include <ostream>

#include <cstdio>
#include <cstring>

#include <arpa/inet.h>
#include <sys/types.h>

namespace vnic {

std::string MacToString(const std::span<const std::uint8_t, 6> mac) {
    char buf[18];
    std::snprintf(buf, sizeof(buf), "%02x:%02x:%02x:%02x:%02x:%02x", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    return buf;
}

std::string Ipv4ToString(std::uint32_t addr) {
    char buf[INET_ADDRSTRLEN];
    in_addr a{};
    a.s_addr = addr;
    inet_ntop(AF_INET, &a, buf, sizeof(buf));
    return buf;
}

std::string Ipv6ToString(std::span<const std::uint8_t, 16> addr) {
    char buf[INET6_ADDRSTRLEN];
    inet_ntop(AF_INET6, addr.data(), buf, sizeof(buf));
    return buf;
}

std::string_view L1Name(Parser::L1Protocol p) {
    switch (p) {
    case Parser::L1Protocol::None: return "None";
    case Parser::L1Protocol::Eth: return "Eth";
    case Parser::L1Protocol::Vlan: return "Vlan";
    case Parser::L1Protocol::Unknown:
    default: return "Unknown";
    }
}

std::string_view L2Name(Parser::L2Protocol p) {
    switch (p) {
    case Parser::L2Protocol::None: return "None";
    case Parser::L2Protocol::Ipv4: return "IPv4";
    case Parser::L2Protocol::Ipv6: return "IPv6";
    case Parser::L2Protocol::Arp: return "ARP";
    case Parser::L2Protocol::Unknown:
    default: return "Unknown";
    }
}

std::string_view L3Name(Parser::L3Protocol p) {
    switch (p) {
    case Parser::L3Protocol::None: return "None";
    case Parser::L3Protocol::Icmp: return "ICMP";
    case Parser::L3Protocol::Tcp: return "TCP";
    case Parser::L3Protocol::Udp: return "UDP";
    case Parser::L3Protocol::Unknown:
    default: return "Unknown";
    }
}

std::string_view ArpOpName(std::uint16_t op) {
    switch (ntohs(op)) {
    case 1: return "request";
    case 2: return "reply";
    case 3: return "RARP-request";
    case 4: return "RARP-reply";
    default: return "?";
    }
}

std::string_view IcmpTypeName(std::uint8_t type) {
    switch (type) {
    case ICMP_ECHOREPLY: return "echo-reply";
    case ICMP_ECHO: return "echo-request";
    case ICMP_DEST_UNREACH: return "dest-unreachable";
    case ICMP_TIME_EXCEEDED: return "time-exceeded";
    case ICMP_REDIRECT: return "redirect";
    default: return "?";
    }
}

std::ostream& operator<<(std::ostream& os, const Parser& p) {
    os << "=== Parsed packet ===\n";

    // ---------- L1 ----------
    os << "[L1] " << L1Name(p.l1Protocol()) << '\n';

    if (const auto eth = p.eth()) {
        os << "  eth.dst = ";
        os << vnic::MacToString(eth->h_dest);
        os << '\n';
        os << "  eth.src = ";
        os << vnic::MacToString(eth->h_source);
        os << '\n';
        os << "  eth.ethertype = 0x" << std::hex << std::setw(4) << std::setfill('0') << ntohs(eth->h_proto) << std::dec
           << std::setfill(' ') << '\n';
    }

    // ---------- L2 ----------
    os << "[L2] " << L2Name(p.l2Protocol()) << '\n';

    switch (p.l2Protocol()) {
    case Parser::L2Protocol::Ipv4: {
        const auto ip = p.ipv4();
        if (!ip) break;
        os << "  ip.version  = " << static_cast<int>(ip->version) << '\n';
        os << "  ip.ihl      = " << static_cast<int>(ip->ihl) << '\n';
        os << "  ip.tos      = " << static_cast<int>(ip->tos) << '\n';
        os << "  ip.tot_len  = " << ntohs(ip->tot_len) << '\n';
        os << "  ip.id       = " << ntohs(ip->id) << '\n';
        os << "  ip.frag_off = " << ntohs(ip->frag_off) << '\n';
        os << "  ip.ttl      = " << static_cast<int>(ip->ttl) << '\n';
        os << "  ip.protocol = " << static_cast<int>(ip->protocol) << '\n';
        os << "  ip.check    = 0x" << std::hex << ntohs(ip->check) << std::dec << '\n';
        os << "  ip.saddr    = " << Ipv4ToString(ip->saddr) << '\n';
        os << "  ip.daddr    = " << Ipv4ToString(ip->daddr) << '\n';
        break;
    }
    case Parser::L2Protocol::Ipv6: {
        const auto ip6 = p.ipv6();
        if (!ip6) break;
        os << "  ip6.version    = " << static_cast<int>(ip6->version) << '\n';
        os << "  ip6.priority   = " << static_cast<int>(ip6->priority) << '\n';
        os << "  ip6.payload_len= " << ntohs(ip6->payload_len) << '\n';
        os << "  ip6.nexthdr   = " << static_cast<int>(ip6->nexthdr) << '\n';
        os << "  ip6.hop_limit  = " << static_cast<int>(ip6->hop_limit) << '\n';
        os << "  ip6.saddr      = " << Ipv6ToString(ip6->saddr.s6_addr) << '\n';
        os << "  ip6.daddr      = " << Ipv6ToString(ip6->daddr.s6_addr) << '\n';
        break;
    }
    case Parser::L2Protocol::Arp: {
        const auto arp = p.arp();
        if (!arp) break;
        os << "  arp.htype = " << ntohs(arp->ar_hrd) << '\n';
        os << "  arp.ptype = 0x" << std::hex << ntohs(arp->ar_pro) << std::dec << '\n';
        os << "  arp.hlen  = " << static_cast<int>(arp->ar_hln) << '\n';
        os << "  arp.plen  = " << static_cast<int>(arp->ar_pln) << '\n';
        os << "  arp.op    = " << ArpOpName(arp->ar_op) << " (" << ntohs(arp->ar_op) << ")\n";

        if (p.hasArpAddresses_) {
            os << "  arp.sha   = " << MacToString(p.arpSenderHw_) << '\n';
            os << "  arp.spa   = " << Ipv4ToString(p.arpSenderProto_) << '\n';
            os << "  arp.tha   = " << MacToString(p.arpTargetHw_) << '\n';
            os << "  arp.tpa   = " << Ipv4ToString(p.arpTargetProto_) << '\n';
        }
        break;
    }
    default: break;
    }

    // ---------- L3 ----------
    os << "[L3] " << L3Name(p.l3Protocol()) << '\n';

    switch (p.l3Protocol()) {
    case Parser::L3Protocol::Icmp: {
        const auto icmp = p.icmp();
        if (!icmp) break;
        os << "  icmp.type = " << IcmpTypeName(icmp->type) << " (" << static_cast<int>(icmp->type) << ")\n";
        os << "  icmp.code = " << static_cast<int>(icmp->code) << '\n';
        os << "  icmp.checksum = 0x" << std::hex << ntohs(icmp->checksum) << std::dec << '\n';
        if (icmp->type == ICMP_ECHO || icmp->type == ICMP_ECHOREPLY) {
            os << "  icmp.id   = " << ntohs(icmp->un.echo.id) << '\n';
            os << "  icmp.seq  = " << ntohs(icmp->un.echo.sequence) << '\n';
        }
        break;
    }
    case Parser::L3Protocol::Tcp: {
        const auto tcp = p.tcp();
        if (!tcp) break;
        os << "  tcp.sport = " << ntohs(tcp->source) << '\n';
        os << "  tcp.dport = " << ntohs(tcp->dest) << '\n';
        os << "  tcp.seq   = " << ntohl(tcp->seq) << '\n';
        os << "  tcp.ack   = " << ntohl(tcp->ack_seq) << '\n';
        os << "  tcp.flags = " << (tcp->fin ? "F" : "") << (tcp->syn ? "S" : "") << (tcp->rst ? "R" : "")
           << (tcp->psh ? "P" : "") << (tcp->ack ? "A" : "") << (tcp->urg ? "U" : "") << '\n';
        break;
    }
    case Parser::L3Protocol::Udp: {
        const auto udp = p.udp();
        if (!udp) break;
        os << "  udp.sport = " << ntohs(udp->source) << '\n';
        os << "  udp.dport = " << ntohs(udp->dest) << '\n';
        os << "  udp.len   = " << ntohs(udp->len) << '\n';
        os << "  udp.check = 0x" << std::hex << ntohs(udp->check) << std::dec << '\n';
        break;
    }
    default: break;
    }

    os << "======================";
    return os;
}

void Parser::reset() noexcept {
    l1_              = L1{};
    l2_              = L2{};
    l3_              = L3{};
    l2Offset_        = 0;
    arpSenderHw_     = {};
    arpTargetHw_     = {};
    arpSenderProto_  = 0;
    arpTargetProto_  = 0;
    hasArpAddresses_ = false;
}

std::expected<void, ErrorCode> Parser::parse(std::span<const std::byte> data) {
    reset();

    if (data.size() < sizeof(ethhdr)) {
        return std::unexpected(ErrorCode::ParseEthernetTooShort);
    }

    //  L1
    l1_.protocol = L1Protocol::Eth;
    const auto eth{utils::LoadUnaligned<ethhdr>(data.data())};
    l1_.eth = eth;
    auto etherType{ntohs(eth.h_proto)};
    std::size_t offset{sizeof(ethhdr)};

    switch (etherType) {
    // VLAN
    case ETH_P_8021Q:
    case ETH_P_8021AD: {
        // Нужно 4 байта: TCI (2) + EtherType (2)
        if (data.size() < offset + 4) {
            return std::unexpected(ErrorCode::ParseTruncated);
        }

        l1_.protocol = L1Protocol::Vlan;

        // TCI - первые 2 байта
        const auto tci = ntohs(utils::LoadUnaligned<__be16>(data.data() + offset));
        l1_.vlanId     = tci & 0x0FFF;       // 12 бит
        l1_.vlanPcp    = (tci >> 13) & 0x07; // 3 бита

        // Внутренний EtherType - следующие 2 байта
        etherType = ntohs(utils::LoadUnaligned<__be16>(data.data() + offset + 2));

        // Сдвинуть offset на 4 байта
        offset += 4;
        break;
    }
    default: {
    }
    }

    //  L2
    l2Offset_ = offset;
    switch (etherType) {
    case ETH_P_IP: return parseIpv4(data, offset);
    case ETH_P_IPV6: return parseIpv6(data, offset);
    case ETH_P_ARP: return parseArp(data, offset);
    default: {
        l2_.protocol = L2Protocol::Unknown;
        return std::unexpected(ErrorCode::ParseUnsupportedEtherType);
    }
    }
    return {};
}

std::expected<void, ErrorCode> Parser::parseIpv4(std::span<const std::byte> data, std::uint64_t offset) {
    if (data.size() < offset + sizeof(iphdr)) {
        return std::unexpected(ErrorCode::ParseIpv4TooShort);
    }

    const auto ipHeader{utils::LoadUnaligned<iphdr>(data.data() + offset)};
    if (ipHeader.version != 4) {
        return std::unexpected(ErrorCode::ParseInvalidIpv4Version);
    }

    l2_.protocol = L2Protocol::Ipv4;
    l2_.ip       = ipHeader;

    //  L3
    const auto ihl = static_cast<std::size_t>(ipHeader.ihl) * 4;
    if (ihl < sizeof(iphdr)) {
        return std::unexpected(ErrorCode::ParseInvalidIpv4HeaderLength);
    }

    const auto l4Offset = offset + ihl;

    switch (ipHeader.protocol) {
    case IPPROTO_TCP: {
        if (data.size() < l4Offset + sizeof(tcphdr)) {
            return std::unexpected(ErrorCode::ParseTcpTooShort);
        }
        l3_.protocol = L3Protocol::Tcp;
        l3_.tcp      = utils::LoadUnaligned<tcphdr>(data.data() + l4Offset);
        break;
    }
    case IPPROTO_UDP: {
        if (data.size() < l4Offset + sizeof(udphdr)) {
            return std::unexpected(ErrorCode::ParseUdpTooShort);
        }
        l3_.protocol = L3Protocol::Udp;
        l3_.udp      = utils::LoadUnaligned<udphdr>(data.data() + l4Offset);
        break;
    }
    case IPPROTO_ICMP: {
        if (data.size() < l4Offset + sizeof(icmphdr)) {
            return std::unexpected(ErrorCode::ParseIcmpTooShort);
        }
        l3_.protocol = L3Protocol::Icmp;
        l3_.icmp     = utils::LoadUnaligned<icmphdr>(data.data() + l4Offset);
        break;
    }
    default: {
        l3_.protocol = L3Protocol::Unknown;
    }
    }
    return {};
}

std::expected<void, ErrorCode> Parser::parseIpv6(std::span<const std::byte> data, std::uint64_t offset) {
    if (data.size() < offset + sizeof(ipv6hdr)) {
        return std::unexpected(ErrorCode::ParseIpv6TooShort);
    }

    const auto ip6Header{utils::LoadUnaligned<ipv6hdr>(data.data() + offset)};
    if (ip6Header.version != 6) {
        return std::unexpected(ErrorCode::ParseInvalidIpv6Version);
    }

    l2_.protocol = L2Protocol::Ipv6;
    l2_.ip6      = ip6Header;

    //  L3 (в IPv6 заголовок фиксированной длины — 40 байт)
    const auto l4Offset = offset + sizeof(ipv6hdr);

    switch (ip6Header.nexthdr) {
    case IPPROTO_TCP: {
        if (data.size() < l4Offset + sizeof(tcphdr)) {
            return std::unexpected(ErrorCode::ParseTcpTooShort);
        }
        l3_.protocol = L3Protocol::Tcp;
        l3_.tcp      = utils::LoadUnaligned<tcphdr>(data.data() + l4Offset);
        break;
    }
    case IPPROTO_UDP: {
        if (data.size() < l4Offset + sizeof(udphdr)) {
            return std::unexpected(ErrorCode::ParseUdpTooShort);
        }
        l3_.protocol = L3Protocol::Udp;
        l3_.udp      = utils::LoadUnaligned<udphdr>(data.data() + l4Offset);
        break;
    }
    case IPPROTO_ICMPV6: {
        if (data.size() < l4Offset + sizeof(icmphdr)) {
            return std::unexpected(ErrorCode::ParseIcmpTooShort);
        }
        l3_.protocol = L3Protocol::Icmp;
        l3_.icmp     = utils::LoadUnaligned<icmphdr>(data.data() + l4Offset);
        break;
    }
    default: {
        l3_.protocol = L3Protocol::Unknown;
    }
    }
    return {};
}

std::expected<void, ErrorCode> Parser::parseArp(std::span<const std::byte> data, std::uint64_t offset) {
    if (data.size() < offset + sizeof(arphdr)) {
        return std::unexpected(ErrorCode::ParseArpTooShort);
    }

    const auto arp{utils::LoadUnaligned<arphdr>(data.data() + offset)};
    const auto arpLength{sizeof(arphdr) + 2 * (static_cast<std::size_t>(arp.ar_hln) + arp.ar_pln)};
    if (data.size() < offset + arpLength) {
        return std::unexpected(ErrorCode::ParseInvalidArpHeaderLength);
    }

    l2_.protocol = L2Protocol::Arp;
    l2_.arp      = arp;

    const auto* raw = data.data() + offset + sizeof(arphdr);
    if (arp.ar_hln == 6 && arp.ar_pln == 4) {
        std::memcpy(arpSenderHw_.data(), raw, arpSenderHw_.size());
        std::memcpy(arpTargetHw_.data(), raw + 10, arpTargetHw_.size());
        arpSenderProto_  = utils::LoadUnaligned<__be32>(raw + 6);
        arpTargetProto_  = utils::LoadUnaligned<__be32>(raw + 16);
        hasArpAddresses_ = true;
    }
    return {};
}

Parser::L1Protocol Parser::l1Protocol() const noexcept { return l1_.protocol; }

std::optional<ethhdr> Parser::eth() const noexcept {
    if (l1_.protocol != L1Protocol::Eth && l1_.protocol != L1Protocol::Vlan) {
        return std::nullopt;
    }
    return l1_.eth;
}

std::uint16_t Parser::vlanId() const noexcept {
    if (l1_.protocol != L1Protocol::Vlan) {
        return 0;
    }
    return l1_.vlanId;
}

std::size_t Parser::l2Offset() const noexcept { return l2Offset_; }

Parser::L2Protocol Parser::l2Protocol() const noexcept { return l2_.protocol; }

std::optional<iphdr> Parser::ipv4() const noexcept {
    if (l2_.protocol != L2Protocol::Ipv4) {
        return std::nullopt;
    }
    return l2_.ip;
}

std::optional<ipv6hdr> Parser::ipv6() const noexcept {
    if (l2_.protocol != L2Protocol::Ipv6) {
        return std::nullopt;
    }
    return l2_.ip6;
}

std::optional<arphdr> Parser::arp() const noexcept {
    if (l2_.protocol != L2Protocol::Arp) {
        return std::nullopt;
    }
    return l2_.arp;
}

Parser::L3Protocol Parser::l3Protocol() const noexcept { return l3_.protocol; }

std::optional<icmphdr> Parser::icmp() const noexcept {
    if (l3_.protocol != L3Protocol::Icmp) {
        return std::nullopt;
    }
    return l3_.icmp;
}

std::optional<tcphdr> Parser::tcp() const noexcept {
    if (l3_.protocol != L3Protocol::Tcp) {
        return std::nullopt;
    }
    return l3_.tcp;
}

std::optional<udphdr> Parser::udp() const noexcept {
    if (l3_.protocol != L3Protocol::Udp) {
        return std::nullopt;
    }
    return l3_.udp;
}

} // namespace vnic
