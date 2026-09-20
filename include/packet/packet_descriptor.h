#pragma once

#include "include/packet/packet_metadata.h"
#include "include/packet/packet_buffer.h"

namespace vnic {

struct PacketDescriptor {
    PacketMetadata metadata;
    PacketBuffer buffer;
};

} // namespace vnic
