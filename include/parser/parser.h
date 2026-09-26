#pragma once

#include "packet/packet_descriptor.h"

#include <span>

#include <cstdint>

namespace vnic {

bool Parse(PacketDescriptor& desc, std::span<const std::byte> data);

} // namespace vnic