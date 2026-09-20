#pragma once

#include "packet/incoming_packet.h"
#include "channel/channel.h"

namespace vnic {

struct Sizeof {
    std::size_t operator() (const IncomingPacket& packet) const {
        return packet.length;
    }
};

class Link : public soft::Channel<IncomingPacket, Sizeof> {
public:
    struct Config : soft::Channel<IncomingPacket, Sizeof>::Config {};

private:
};

} // namespace vnic
