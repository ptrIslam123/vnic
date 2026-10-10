#pragma once

#include "constanst.h"
#include "parser/parser.h"

#include <span>
#include <ostream>

#include <cstdint>

namespace vnic {

struct PacketBuffer {
    PacketBuffer* next{}; // Следующий сегмент, содержащий остальную часть сетевого пакета(для Jumbo Frames / TSO)
    std::uint32_t length{};   // Длина данных в ЭТОМ сегменте
    // ... payload

    std::span<std::byte> getPacket() {
        return std::span<std::byte>{
            reinterpret_cast<std::byte*>(this) + sizeof(PacketBuffer),
            static_cast<std::size_t>(length)
        };
    }

    std::span<const std::byte> getPacket() const {
        return std::span<const std::byte>{
            reinterpret_cast<const std::byte*>(this) + sizeof(PacketBuffer),
            static_cast<std::size_t>(length)
        };
    }

    std::ostream& operator<<(std::ostream& os) const {
        Parser parser;
        if (parser.parse(getPacket())) {
            os << parser;
        } else {
            os << "Invalid packet metadata";
        }
        os << "\n";
        return os;
    }
};

inline std::ostream& operator<<(std::ostream& os, const PacketBuffer& pb) {
    return pb.operator<<(os);
}

} // namespace vnic
