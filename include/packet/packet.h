#pragma once

#include <concepts>

#include <cstdint>

namespace vnic {

template<typename Func, typename Packet>
concept SegmentVisitor = requires(Func func, Packet& p) {
    { func(p) } -> std::convertible_to<bool>;
};

template<typename Func, typename Packet>
concept ConstSegmentVisitor = requires(Func func, const Packet& p) {
    { func(p) } -> std::convertible_to<bool>;
};

template<std::size_t N>
struct FixedPacket {
    static constexpr auto SEGMENT_SIZE{N};

    std::uint64_t timestamp;    // Время приёма/передачи
    std::uint32_t hash;      // RSS-хэш
    std::uint16_t queueId;      // Номер очереди
    std::uint32_t offloads;     // Флаги оффлоадов
    std::uint16_t vlanId;       // VLAN-тег
    std::uint32_t length = 0;        // Длина данных в ЭТОМ сегменте
    std::uint32_t totalLength = 0;   // Общая длина всех сегментов
    FixedPacket* next; // Следующий сегмент (для Jumbo Frames / TSO)
    std::uint8_t data[N];

    template<typename Func>
        requires SegmentVisitor<Func, FixedPacket>
    void forEachSegment(Func func) {
        for (FixedPacket* p = this; p != nullptr; p = p->next) {
            if (!func(*p)) {
                break;
            }
        }
    }

    template<typename Func>
        requires ConstSegmentVisitor<Func, FixedPacket>
    void forEachSegment(Func func) const {
        for (const FixedPacket* p = this; p != nullptr; p = p->next) {
            if (!func(*p)) {
                break;
            }
        }
    }
};

using Packet = FixedPacket<2048>;

struct PacketDescriptor {
    void* data;                  // Указатель на буфер пакета
    std::uint32_t length;        // Длина данных в буфере
    std::uint64_t timestamp;     // Временная метка (если поддерживается)
    std::uint32_t flags;         // Статус, оффлоады и т.д.

    template<std::size_t N>
    void setMetadata(const FixedPacket<N>& packet) {
        length    = packet.length;
        timestamp = packet.timestamp;
        flags     = packet.offloads;
        // data НЕ трогаем - он уже указывает на буфер
    }
};

} // namespace vnic
