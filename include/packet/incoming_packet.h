#pragma once

#include <cstdint>

namespace vnic {

// Пакет, который приходит на NIC (внутренний)
struct IncomingPacket {
    std::uint8_t* data;
    std::uint32_t length;
    std::uint32_t hash;
    std::uint16_t queueId;
    std::uint32_t offloads;
};

} // namespace vnic
