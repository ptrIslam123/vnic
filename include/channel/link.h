#pragma once

#include "packet/packet.h"
#include "channel/channel.h"

namespace vnic {

struct Sizeof {
    std::size_t operator() (const Packet& packet) const {
        std::size_t bytes{0};
        packet.forEachSegment([&](const Packet& packet) {
            bytes += packet.length;
            return true;
        });
        return bytes;
    }
};

class Link : public soft::Channel<Packet, Sizeof> {
public:
    struct Config : soft::Channel<Packet, Sizeof>::Config {};

private:
};

} // namespace vnic
