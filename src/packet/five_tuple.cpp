#include "packet/five_tuple.h"

#include <linux/ip.h>
#include <linux/ipv6.h>
#include <linux/udp.h>
#include <linux/tcp.h>

namespace vnic {

// Строит 5-tuple только для IP-кадров (IPv4 или IPv6). Для не-IP (ARP и т.п.)
// возвращает нулевой tuple, не разыменовывая нулевой указатель.
FiveTuple Get5Tuple(const Parser& parser) {
    FiveTuple tuple{};

    if (const auto* ip{parser.ipv4()}) {
        tuple.version = FiveTuple::kIpv4;
        tuple.saddr4  = ip->saddr;
        tuple.daddr4  = ip->daddr;
        tuple.iproto  = static_cast<std::uint8_t>(ip->protocol);
    } else if (const auto* ip6{parser.ipv6()}) {
        tuple.version = FiveTuple::kIpv6;
        tuple.saddr6  = ip6->saddr;
        tuple.daddr6  = ip6->daddr;
        tuple.iproto  = ip6->nexthdr;
    } else {
        return tuple; // не IP — 5-tuple не строим
    }

    switch (parser.l3Protocol()) {
    case Parser::L3Protocol::Tcp: {
        if (const auto* tcp{parser.tcp()}) {
            tuple.sport = tcp->source;
            tuple.dport = tcp->dest;
        }
        break;
    }
    case Parser::L3Protocol::Udp: {
        if (const auto* udp{parser.udp()}) {
            tuple.sport = udp->source;
            tuple.dport = udp->dest;
        }
        break;
    }
    default: break;
    }

    return tuple;
}

} // namespace vnic
