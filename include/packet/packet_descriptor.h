#pragma once

#include "packet/packet_buffer.h"

#include <ostream>

#include <cstdint>

namespace vnic {

struct PacketDescriptor {
    std::uint64_t timestamp{};    // Время приёма/передачи
    std::uint32_t hash{};      // RSS-хэш
    std::uint16_t queueId{};      // Номер очереди
    std::uint32_t offloads{};     // Флаги оффлоадов
    std::uint16_t vlanId{};       // VLAN-тег
    std::uint32_t totalLength{};   // Общая длина всех сегментов
    PacketBuffer* buffer{};

    std::ostream& operator<<(std::ostream& os) const {
        os << "[Packet]"
           << " queue_id=" << queueId
           << " hash=0x" << std::hex << hash << std::dec
           << " len=" << totalLength
           << " vlan=" << vlanId
           << " offloads=0x" << std::hex << offloads << std::dec
           << " timestamp=" << timestamp
           << " buffer=" << static_cast<const void*>(buffer);
        return os;
    }
};

inline std::ostream& operator<<(std::ostream& os, const PacketDescriptor& pd) {
    return pd.operator<<(os);
}

} // namespace vnic
