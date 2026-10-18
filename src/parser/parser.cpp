#include "parser/parser.h"

#include <iomanip>
#include <ostream>
#include <sstream>

#include <net/ethernet.h>
#include <netinet/ip.h>
#include <linux/ipv6.h>
#include <netinet/ip_icmp.h>
#include <netinet/tcp.h>
#include <netinet/udp.h>
#include <net/if_arp.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>

namespace {

using namespace vnic;

std::string macToString(const std::uint8_t* mac) {
    char buf[18];
    std::snprintf(buf, sizeof(buf), "%02x:%02x:%02x:%02x:%02x:%02x", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    return buf;
}

std::string ipv4ToString(std::uint32_t addr) {
    char buf[INET_ADDRSTRLEN];
    in_addr a{};
    a.s_addr = addr;
    inet_ntop(AF_INET, &a, buf, sizeof(buf));
    return buf;
}

std::string ipv6ToString(const std::uint8_t addr[16]) {
    char buf[INET6_ADDRSTRLEN];
    inet_ntop(AF_INET6, addr, buf, sizeof(buf));
    return buf;
}

const char* l1Name(Parser::L1Protocol p) {
    switch (p) {
    case Parser::L1Protocol::None: return "None";
    case Parser::L1Protocol::Eth: return "Eth";
    case Parser::L1Protocol::Vlan: return "Vlan";
    case Parser::L1Protocol::Unknown: return "Unknown";
    }
    return "?";
}

const char* l2Name(Parser::L2Protocol p) {
    switch (p) {
    case Parser::L2Protocol::None: return "None";
    case Parser::L2Protocol::Ipv4: return "IPv4";
    case Parser::L2Protocol::Ipv6: return "IPv6";
    case Parser::L2Protocol::Arp: return "ARP";
    case Parser::L2Protocol::Unknown: return "Unknown";
    }
    return "?";
}

const char* l3Name(Parser::L3Protocol p) {
    switch (p) {
    case Parser::L3Protocol::None: return "None";
    case Parser::L3Protocol::Icmp: return "ICMP";
    case Parser::L3Protocol::Tcp: return "TCP";
    case Parser::L3Protocol::Udp: return "UDP";
    case Parser::L3Protocol::Unknown: return "Unknown";
    }
    return "?";
}

const char* arpOpName(std::uint16_t op) {
    switch (ntohs(op)) {
    case 1: return "request";
    case 2: return "reply";
    case 3: return "RARP-request";
    case 4: return "RARP-reply";
    default: return "?";
    }
}

const char* icmpTypeName(std::uint8_t type) {
    switch (type) {
    case ICMP_ECHOREPLY: return "echo-reply";
    case ICMP_ECHO: return "echo-request";
    case ICMP_DEST_UNREACH: return "dest-unreachable";
    case ICMP_TIME_EXCEEDED: return "time-exceeded";
    case ICMP_REDIRECT: return "redirect";
    default: return "?";
    }
}

void printMac(std::ostream& os, const std::uint8_t* mac) { os << macToString(mac); }

} // namespace

namespace vnic {

std::ostream& operator<<(std::ostream& os, const Parser& p) {
    os << "=== Parsed packet ===\n";

    // ---------- L1 ----------
    os << "[L1] " << l1Name(p.l1Protocol()) << '\n';

    if (const ethhdr* eth = p.eth()) {
        os << "  eth.dst = ";
        printMac(os, eth->h_dest);
        os << '\n';
        os << "  eth.src = ";
        printMac(os, eth->h_source);
        os << '\n';
        os << "  eth.ethertype = 0x" << std::hex << std::setw(4) << std::setfill('0') << ntohs(eth->h_proto) << std::dec
           << std::setfill(' ') << '\n';
    }

    // ---------- L2 ----------
    os << "[L2] " << l2Name(p.l2Protocol()) << '\n';

    switch (p.l2Protocol()) {
    case Parser::L2Protocol::Ipv4: {
        const iphdr* ip = p.ipv4();
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
        os << "  ip.saddr    = " << ipv4ToString(ip->saddr) << '\n';
        os << "  ip.daddr    = " << ipv4ToString(ip->daddr) << '\n';
        break;
    }
    case Parser::L2Protocol::Ipv6: {
        const ipv6hdr* ip6 = p.ipv6();
        if (!ip6) break;
        os << "  ip6.version    = " << static_cast<int>(ip6->version) << '\n';
        os << "  ip6.priority   = " << static_cast<int>(ip6->priority) << '\n';
        os << "  ip6.payload_len= " << ntohs(ip6->payload_len) << '\n';
        os << "  ip6.nexthdr   = " << static_cast<int>(ip6->nexthdr) << '\n';
        os << "  ip6.hop_limit  = " << static_cast<int>(ip6->hop_limit) << '\n';
        os << "  ip6.saddr      = " << ipv6ToString(ip6->saddr.s6_addr) << '\n';
        os << "  ip6.daddr      = " << ipv6ToString(ip6->daddr.s6_addr) << '\n';
        break;
    }
    case Parser::L2Protocol::Arp: {
        const arphdr* arp = p.arp();
        if (!arp) break;
        os << "  arp.htype = " << ntohs(arp->ar_hrd) << '\n';
        os << "  arp.ptype = 0x" << std::hex << ntohs(arp->ar_pro) << std::dec << '\n';
        os << "  arp.hlen  = " << static_cast<int>(arp->ar_hln) << '\n';
        os << "  arp.plen  = " << static_cast<int>(arp->ar_pln) << '\n';
        os << "  arp.op    = " << arpOpName(arp->ar_op) << " (" << ntohs(arp->ar_op) << ")\n";

        // ARP-адреса лежат сразу после arphdr в «сыром» виде
        const auto* raw = reinterpret_cast<const std::uint8_t*>(arp) + sizeof(arphdr);
        if (arp->ar_hln == 6 && arp->ar_pln == 4) {
            const std::uint8_t* sha = raw;      // sender MAC
            const std::uint8_t* spa = raw + 6;  // sender IP
            const std::uint8_t* tha = raw + 10; // target MAC
            const std::uint8_t* tpa = raw + 16; // target IP

            os << "  arp.sha   = " << macToString(sha) << '\n';
            os << "  arp.spa   = " << ipv4ToString(*reinterpret_cast<const std::uint32_t*>(spa)) << '\n';
            os << "  arp.tha   = " << macToString(tha) << '\n';
            os << "  arp.tpa   = " << ipv4ToString(*reinterpret_cast<const std::uint32_t*>(tpa)) << '\n';
        }
        break;
    }
    default: break;
    }

    // ---------- L3 ----------
    os << "[L3] " << l3Name(p.l3Protocol()) << '\n';

    switch (p.l3Protocol()) {
    case Parser::L3Protocol::Icmp: {
        const icmphdr* icmp = p.icmp();
        if (!icmp) break;
        os << "  icmp.type = " << icmpTypeName(icmp->type) << " (" << static_cast<int>(icmp->type) << ")\n";
        os << "  icmp.code = " << static_cast<int>(icmp->code) << '\n';
        os << "  icmp.checksum = 0x" << std::hex << ntohs(icmp->checksum) << std::dec << '\n';
        if (icmp->type == ICMP_ECHO || icmp->type == ICMP_ECHOREPLY) {
            os << "  icmp.id   = " << ntohs(icmp->un.echo.id) << '\n';
            os << "  icmp.seq  = " << ntohs(icmp->un.echo.sequence) << '\n';
        }
        break;
    }
    case Parser::L3Protocol::Tcp: {
        const tcphdr* tcp = p.tcp();
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
        const udphdr* udp = p.udp();
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

bool Parser::parse(std::span<const std::byte> data) {
    if (data.size() < sizeof(ethhdr)) {
        return false;
    }

    //  L1
    l1_.protocol = L1Protocol::Eth;
    l1_.eth      = reinterpret_cast<const ethhdr*>(data.data());
    auto etherType{ntohs(l1_.eth->h_proto)};
    std::size_t offset{sizeof(ethhdr)};

    switch (etherType) {
    // VLAN
    case ETH_P_8021Q:
    case ETH_P_8021AD: {
        // Нужно 4 байта: TCI (2) + EtherType (2)
        if (data.size() < offset + 4) {
            return false;
        }

        l1_.protocol = L1Protocol::Vlan;
        l1_.vlan     = reinterpret_cast<const std::uint8_t*>(data.data() + offset);

        // TCI - первые 2 байта
        const auto tci = ntohs(*reinterpret_cast<const __be16*>(l1_.vlan));
        l1_.vlanId     = tci & 0x0FFF;       // 12 бит
        l1_.vlanPcp    = (tci >> 13) & 0x07; // 3 бита

        // Внутренний EtherType - следующие 2 байта
        etherType = ntohs(*reinterpret_cast<const __be16*>(l1_.vlan + 2));

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
        return false;
    }
    }
    return true;
}

bool Parser::parseIpv4(std::span<const std::byte> data, std::uint64_t offset) {
    if (data.size() < offset + sizeof(iphdr)) {
        return false;
    }

    const auto* ip = reinterpret_cast<const iphdr*>(data.data() + offset);
    if (ip->version != 4) {
        return false;
    }

    l2_.protocol = L2Protocol::Ipv4;
    l2_.hdr.ip   = ip;

    //  L3
    const auto ihl = static_cast<std::size_t>(ip->ihl) * 4;
    if (ihl < sizeof(iphdr)) {
        return false;
    }

    const auto l4Offset = offset + ihl;

    switch (ip->protocol) {
    case IPPROTO_TCP: {
        if (data.size() < l4Offset + sizeof(tcphdr)) {
            return false;
        }
        l3_.protocol = L3Protocol::Tcp;
        l3_.hdr.tcp  = reinterpret_cast<const tcphdr*>(data.data() + l4Offset);
        break;
    }
    case IPPROTO_UDP: {
        if (data.size() < l4Offset + sizeof(udphdr)) {
            return false;
        }
        l3_.protocol = L3Protocol::Udp;
        l3_.hdr.udp  = reinterpret_cast<const udphdr*>(data.data() + l4Offset);
        break;
    }
    case IPPROTO_ICMP: {
        if (data.size() < l4Offset + sizeof(icmphdr)) {
            return false;
        }
        l3_.protocol = L3Protocol::Icmp;
        l3_.hdr.icmp = reinterpret_cast<const icmphdr*>(data.data() + l4Offset);
        break;
    }
    default: {
        l3_.protocol = L3Protocol::Unknown;
    }
    }
    return true;
}

bool Parser::parseIpv6(std::span<const std::byte> data, std::uint64_t offset) {
    if (data.size() < offset + sizeof(ipv6hdr)) {
        return false;
    }

    const auto* ip6 = reinterpret_cast<const ipv6hdr*>(data.data() + offset);
    if (ip6->version != 6) {
        return false;
    }

    l2_.protocol = L2Protocol::Ipv6;
    l2_.hdr.ip6  = ip6;

    //  L3 (в IPv6 заголовок фиксированной длины — 40 байт)
    const auto l4Offset = offset + sizeof(ipv6hdr);

    switch (ip6->nexthdr) {
    case IPPROTO_TCP: {
        if (data.size() < l4Offset + sizeof(tcphdr)) {
            return false;
        }
        l3_.protocol = L3Protocol::Tcp;
        l3_.hdr.tcp  = reinterpret_cast<const tcphdr*>(data.data() + l4Offset);
        break;
    }
    case IPPROTO_UDP: {
        if (data.size() < l4Offset + sizeof(udphdr)) {
            return false;
        }
        l3_.protocol = L3Protocol::Udp;
        l3_.hdr.udp  = reinterpret_cast<const udphdr*>(data.data() + l4Offset);
        break;
    }
    case IPPROTO_ICMPV6: {
        if (data.size() < l4Offset + sizeof(icmphdr)) {
            return false;
        }
        l3_.protocol = L3Protocol::Icmp;
        l3_.hdr.icmp = reinterpret_cast<const icmphdr*>(data.data() + l4Offset);
        break;
    }
    default: {
        l3_.protocol = L3Protocol::Unknown;
    }
    }
    return true;
}

bool Parser::parseArp(std::span<const std::byte> data, std::uint64_t offset) {
    if (data.size() < offset + sizeof(arphdr)) {
        return false;
    }

    l2_.protocol = L2Protocol::Arp;
    l2_.hdr.arp  = reinterpret_cast<const arphdr*>(data.data() + offset);
    return true;
}

Parser::L1Protocol Parser::l1Protocol() const noexcept { return l1_.protocol; }

const ethhdr* Parser::eth() const noexcept {
    if (l1_.protocol != L1Protocol::Eth && l1_.protocol != L1Protocol::Vlan) {
        return nullptr;
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

const iphdr* Parser::ipv4() const noexcept {
    if (l2_.protocol != L2Protocol::Ipv4) {
        return nullptr;
    }
    return l2_.hdr.ip;
}

const ipv6hdr* Parser::ipv6() const noexcept {
    if (l2_.protocol != L2Protocol::Ipv6) {
        return nullptr;
    }
    return l2_.hdr.ip6;
}

const arphdr* Parser::arp() const noexcept {
    if (l2_.protocol != L2Protocol::Arp) {
        return nullptr;
    }
    return l2_.hdr.arp;
}

Parser::L3Protocol Parser::l3Protocol() const noexcept { return l3_.protocol; }

const icmphdr* Parser::icmp() const noexcept {
    if (l3_.protocol != L3Protocol::Icmp) {
        return nullptr;
    }
    return l3_.hdr.icmp;
}

const tcphdr* Parser::tcp() const noexcept {
    if (l3_.protocol != L3Protocol::Tcp) {
        return nullptr;
    }
    return l3_.hdr.tcp;
}

const udphdr* Parser::udp() const noexcept {
    if (l3_.protocol != L3Protocol::Udp) {
        return nullptr;
    }
    return l3_.hdr.udp;
}

} // namespace vnic