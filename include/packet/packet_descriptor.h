#pragma once

#include "include/packet/packet_buffer.h"

namespace vnic {

struct PacketDescriptor {
    std::uint64_t timestamp;    // Время приёма/передачи
    std::uint32_t hash;      // RSS-хэш
    std::uint16_t queueId;      // Номер очереди
    std::uint32_t offloads;     // Флаги оффлоадов
    std::uint16_t vlanId;       // VLAN-тег
    std::uint32_t totalLength = 0;   // Общая длина всех сегментов
    PacketBuffer* buffer;
};



} // namespace vnic
