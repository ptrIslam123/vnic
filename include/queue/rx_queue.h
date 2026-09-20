#pragma once

#include "queue/queue.h"
#include "packet/packet_buffer.h"
#include "packet/incoming_packet.h"
#include "constanst.h"

#include <array>
#include <algorithm>
#include <span>
#include <vector>

#include <cstring>
#include <cassert>

namespace vnic {

class RxQueue final : public Queue {
public:
    struct Config : Queue::Config {};

    bool put(IncomingPacket&& packet);
    std::uint64_t take(std::vector<PacketDescriptor>& descs);
    void free(std::vector<PacketDescriptor>&& descs);
};

} // namespace vnic