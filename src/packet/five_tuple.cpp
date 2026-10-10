#include "packet/five_tuple.h"

#include <linux/ip.h>
#include <linux/udp.h>
#include <linux/tcp.h>

#include <cassert>

namespace vnic {

FiveTuple Get5Tuple(const Parser& parser) {
    FiveTuple tuple{};
    assert(parser.l2Protocol() == Parser::L2Protocol::Ipv4 ||
           parser.l2Protocol() == Parser::L2Protocol::Ipv6);

    const auto* ip{parser.ipv4()};
    assert(ip);
    tuple.sadr = ip->saddr;
    tuple.dadr = ip->daddr;
    tuple.iproto = static_cast<std::uint8_t>(ip->protocol);

    switch (parser.l3Protocol()) {
        case Parser::L3Protocol::Tcp: {
            const auto* tcp{parser.tcp()};
            assert(tcp);
            tuple.sport = tcp->source;
            tuple.dport = tcp->dest;
            break;
        }
        case Parser::L3Protocol::Udp: {
            const auto* udp{parser.udp()};
            assert(udp);
            tuple.sport = udp->source;
            tuple.dport = udp->dest;
            break;
        }
        default:
            break;
    }

    return tuple;
}

} // namespace vnic
