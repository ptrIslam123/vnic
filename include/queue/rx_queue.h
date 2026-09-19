#pragma once

#include "include/queue/queue.h"
#include "include/packet/packet.h"

namespace vnic {

class RxQueue final : public Queue {
public:
    struct Config : Queue::Config {};

    std::uint64_t put(Packet&& packet);
    std::uint64_t take(std::vector<PacketDescriptor>& descs);
    void free(std::vector<PacketDescriptor>& descs);
};

} // namespace vnic
