#pragma once

#include "packet/packet_metadata.h"
#include "packet/five_tuple.h"

#include <cstdint>

namespace vnic {

// Пакет, который приходит на NIC (внутренний).
//
// Это "вид" пакета на линии: просто указатель на данные + метаданные,
// без владения буфером. Сам буфер принадлежит отправителю (стек/приложение),
// а RX-очередь при приёме копирует данные в свой memory pool.
struct IncomingPacket {
    std::uint8_t* data;
    std::uint32_t length;
    std::uint32_t hash;
    std::uint16_t queueId;
    std::uint32_t offloads;

    PacketMetadata getMetadata() const noexcept {
        return PacketMetadata{
            .timestamp   = 0,          // TODO: время приёма
            .hash        = hash,
            .queueId     = queueId,
            .offloads    = offloads,
            .vlanId      = 0,          // TODO: VLAN-тег
            .totalLength = length
        };
    }

    // RSS-пятерка. Каркас: умеем разбирать только Ethernet + IPv4,
    // для TCP/UDP захватываем ещё и порты. Вне v1 — IPv6, VLAN, SCTP.
    FiveTuple get5Tuple() const noexcept {
        FiveTuple tuple{};
        return tuple; // TODO
    }
};

} // namespace vnic