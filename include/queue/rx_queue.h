#pragma once

#include "constanst.h"
#include "queue/queue.h"
#include "packet/packet_descriptor.h"

#include <array>
#include <algorithm>
#include <span>
#include <vector>

#include <cstring>
#include <cassert>
#include <cstdint>

namespace vnic {

class RxQueue final : public Queue {
public:
    struct Config : Queue::Config {};

    bool put(PacketDescriptor&& packet, std::span<const std::byte> payload);
    std::uint64_t take(std::vector<PacketDescriptor>& descs);
    void free(std::vector<PacketDescriptor>&& descs);
};

} // namespace vnic