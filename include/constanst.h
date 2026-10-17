#pragma once

#include <cstdint>

namespace vnic {

inline constexpr auto MAX_SEGMENTS{512};
inline constexpr auto PACKET_BUFFER_SIZE{2048};
// Минимальный размер кадра на проводе (60 байт данных + 4 байта FCS).
inline constexpr std::size_t MIN_FRAME_SIZE{64};

} // namespace vnic
