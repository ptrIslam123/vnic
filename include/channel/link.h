#pragma once

#include "packet/packet.h"
#include "channel/channel.h"

namespace vnic {

class Link : public soft::Channel<Packet> {
public:

private:
};

} // namespace vnic
