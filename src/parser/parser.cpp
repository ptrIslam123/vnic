#include "parser/parser.h"
#include "parser/vlan_ethhdr.h"

#include <linux/if_ether.h>
#include <linux/ip.h>
#include <linux/ipv6.h>
#include <linux/arp.h>
#include <linux/icmp.h>
#include <linux/tcp.h>
#include <linux/udp.h>

#include <arpa/inet.h>

namespace vnic {

bool Parser::parse(std::span<const std::byte> data) {
    if (data.size() < sizeof(ethhdr)) {
        return false;
    }

    //  L1
    l1_.protocol = L1Protocol::Eth;
    l1_.eth = reinterpret_cast<const ethhdr*>(data.data());
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
            l1_.vlan = reinterpret_cast<const std::uint8_t*>(data.data() + offset);

            // TCI - первые 2 байта
            const auto tci = ntohs(*reinterpret_cast<const __be16*>(l1_.vlan));
            l1_.vlanId  = tci & 0x0FFF;          // 12 бит
            l1_.vlanPcp = (tci >> 13) & 0x07;    // 3 бита

            // Внутренний EtherType - следующие 2 байта
            etherType = ntohs(*reinterpret_cast<const __be16*>(l1_.vlan + 2));

            // Сдвинуть offset на 4 байта
            offset += 4;
            break;
        }
        default: {}
    }

    //  L2
    switch (etherType) {
        case ETH_P_IP: return parseIpv4(data, offset);
        case ETH_P_ARP: return parseArp(data, offset);
        default: {
            l2_.protocol = L2Protocol::Unknown;
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

    l2_.protocol = L2Protocol::Ip;
    l2_.hdr.ip = ip;

    //  L3
    const auto ihl = ip->ihl * 4;
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
            l3_.hdr.tcp = reinterpret_cast<const tcphdr*>(data.data() + l4Offset);
            break;
        }
        case IPPROTO_UDP: {
            if (data.size() < l4Offset + sizeof(udphdr)) {
                return false;
            }
            l3_.protocol = L3Protocol::Udp;
            l3_.hdr.udp = reinterpret_cast<const udphdr*>(data.data() + l4Offset);
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

bool Parser::parseArp(std::span<const std::byte> data, std::uint64_t offset) {
    if (data.size() < offset + sizeof(arphdr)) {
        return false;
    }

    l2_.protocol = L2Protocol::Arp;
    l2_.hdr.arp = reinterpret_cast<const arphdr*>(data.data() + offset);
    return true;
}

Parser::L1Protocol Parser::l1Protocol() const noexcept {
    return l1_.protocol;
}

const ethhdr* Parser::eth() const noexcept {
    if (l1_.protocol != L1Protocol::Eth) {
        return nullptr;
    }
    return l1_.eth;
}

Parser::L2Protocol Parser::l2Protocol() const noexcept {
    return l2_.protocol;
}

const iphdr* Parser::ip() const noexcept {
    if (l2_.protocol != L2Protocol::Ip) {
        return nullptr;
    }
    return l2_.hdr.ip;
}

const ipv6hdr* Parser::ip6() const noexcept {
    if (l2_.protocol != L2Protocol::Ip6) {
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

Parser::L3Protocol Parser::l3Protocol() const noexcept {
    return l3_.protocol;
}

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