#pragma once

#include "include/constanst.h"

#include <cstdint>

namespace vnic {

template<std::uint64_t N>
struct BasePacketBuffer {
    std::uint32_t length;   // Длина данных в ЭТОМ сегменте
    std::uint8_t* memory;     // Указатель на начало непрерывного сегмента данных пакета
    BasePacketBuffer* next; // Следующий сегмент, содержащий остальную часть сетевого пакета(для Jumbo Frames / TSO)
};

using PacketBuffer = BasePacketBuffer<PACKET_BUFFER_SIZE>;

} // namespace vnic
