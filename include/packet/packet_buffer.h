#pragma once

#include "include/constanst.h"

#include <cstdint>

namespace vnic {

struct PacketBuffer {
    std::uint16_t queueId;
    PacketBuffer* next; // Следующий сегмент, содержащий остальную часть сетевого пакета(для Jumbo Frames / TSO)
    std::uint32_t length;   // Длина данных в ЭТОМ сегменте
    // ... payload
};

} // namespace vnic
