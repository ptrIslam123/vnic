#include "validate/validate.h"

#include "parser/parser.h"

#include <cstdio>
#include <cstring>

#include <arpa/inet.h>
#include <net/ethernet.h>
#include <netinet/ip.h>

namespace vnic::validate {

namespace {

constexpr std::size_t kIpv4MinHeaderBytes{20};
constexpr std::size_t kEthernetHeaderBytes{14};
constexpr std::array<std::uint8_t, 6> kBroadcastMac{0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

// Broadcast: все байты адреса равны 0xFF.
bool IsBroadcast(const std::uint8_t* mac) { return std::memcmp(mac, kBroadcastMac.data(), kBroadcastMac.size()) == 0; }

// Multicast: младший бит первого байта MAC-адреса выставлен (I/G bit).
bool IsMulticast(const std::uint8_t* mac) { return (mac[0] & 0x01U) != 0; }

} // namespace

std::optional<std::array<std::uint8_t, 6>> ParseMac(const std::string& mac) {
    std::array<std::uint8_t, 6> out{};
    unsigned b0{}, b1{}, b2{}, b3{}, b4{}, b5{};
    if (std::sscanf(mac.c_str(), "%x:%x:%x:%x:%x:%x", &b0, &b1, &b2, &b3, &b4, &b5) != 6) {
        return std::nullopt;
    }
    const unsigned parts[6]{b0, b1, b2, b3, b4, b5};
    for (std::size_t i = 0; i < out.size(); ++i) {
        if (parts[i] > 0xFFU) {
            return std::nullopt;
        }
        out[i] = static_cast<std::uint8_t>(parts[i]);
    }
    return out;
}

// Проверка — размер кадра (runts и MTU).
// Реальный NIC отбрасывает «короткие» кадры (runts); на проводе это 64 байта
// (60 данных + 4 FCS). В этой модели Link передаёт байты БЕЗ FCS, поэтому
// порог minFrameSize = 60 байт. Слишком короткий кадр считается ошибкой MAC/PHY
// и не доставляется приложению.
// Сверху MTU ограничивает размер L3-нагрузки (заголовок IP + данные), а не
// полного кадра: на проводе к ней добавляется заголовок канального уровня (L2),
// включая возможный VLAN-тег. Поэтому верхняя граница — l2Offset + mtu.
bool CheckFrameSize(std::span<const std::byte> packet, const Parser& parser, std::size_t minFrameSize,
                    std::uint32_t mtu) {
    const auto l2HeaderBytes{parser.l2Offset() != 0 ? parser.l2Offset() : kEthernetHeaderBytes};
    return packet.size() >= minFrameSize && packet.size() <= l2HeaderBytes + mtu;
}

// Проверка — контрольная последовательность кадра (FCS/CRC32).
// Реальный NIC проверяет 4-байтовый FCS и отбрасывает кадры с ошибкой.
// В текущей архитектуре Link оперирует «чистым» потоком байт БЕЗ FCS (труба
// передаёт только payload — осознанное решение), поэтому проверять нечего:
// функция всегда возвращает true. Слот оставлен под будущую эмуляцию FCS на
// уровне Link; тогда здесь появится реальный расчёт CRC32.
bool CheckFcs(std::span<const std::byte> packet) {
    (void)packet;
    return true;
}

// Проверка — фильтр MAC-адреса получателя.
// Реальный NIC принимает кадр, только если dst MAC совпадает с собственным
// unicast-адресом карты, является broadcast (ff:ff:ff:ff:ff:ff) или multicast.
// В promiscuous-режиме фильтр отключён и принимается любой адрес назначения.
bool CheckDstMac(const Parser& parser, const std::array<std::uint8_t, 6>& macAddr, bool promiscuousEnabled) {
    if (promiscuousEnabled) {
        return true;
    }

    const auto* eth = parser.eth();
    if (eth == nullptr) {
        return true; // не Ethernet-кадр — фильтровать по MAC нечего
    }

    if (std::memcmp(eth->h_dest, macAddr.data(), macAddr.size()) == 0) {
        return true;
    }
    if (IsBroadcast(eth->h_dest)) {
        return true;
    }
    if (IsMulticast(eth->h_dest)) {
        return true;
    }
    return false;
}

// Проверка — VLAN-фильтр.
// Реальный NIC принимает VLAN-тегированные кадры, только если VLAN ID входит в
// разрешённый список (802.1Q VLAN filter). Пустой список означает, что фильтр
// выключен и принимаются все VLAN (поведение по умолчанию до его настройки).
// Кадры без VLAN-тега проверку проходят всегда.
bool CheckVlanFilter(const Parser& parser, const std::vector<std::uint16_t>& allowList) {
    if (parser.l1Protocol() != Parser::L1Protocol::Vlan) {
        return true;
    }
    if (allowList.empty()) {
        return true;
    }

    const auto vlan = parser.vlanId();
    for (const auto allowed : allowList) {
        if (allowed == vlan) {
            return true;
        }
    }
    return false;
}

// Проверка — длина IPv4, заявленная в заголовке (total length).
// Реальный NIC сверяет поле total length IPv4-заголовка с фактическим объёмом
// полученных байт: расхождение означает усечённый или подделанный кадр, такой
// пакет отбрасывается. Проверяется только для IPv4; остальные кадры проходят.
bool CheckIpTotalLength(std::span<const std::byte> packet, const Parser& parser) {
    if (parser.l2Protocol() != Parser::L2Protocol::Ipv4) {
        return true;
    }

    const auto* ip = parser.ipv4();
    if (ip == nullptr) {
        return true;
    }

    const auto headerBytes = static_cast<std::size_t>(ip->ihl) * 4;
    const auto totalLength = static_cast<std::size_t>(ntohs(ip->tot_len));
    const auto l2Offset    = parser.l2Offset();

    if (headerBytes < kIpv4MinHeaderBytes) {
        return false;
    }
    if (totalLength < headerBytes) {
        return false;
    }
    if (l2Offset + totalLength > packet.size()) {
        return false;
    }
    return true;
}

// Проверка — контрольная сумма IPv4-заголовка.
// Реальный NIC (при включённой проверке IP) пересчитывает checksum ТОЛЬКО
// заголовка IPv4 (не payload) и отбрасывает пакет при ошибке. Здесь выполняется
// программный пересчёт: сумма 16-битных слов в one's complement должна дать
// 0xFFFF. Проверяется только для IPv4; остальные кадры проходят.
bool CheckIpv4Checksum(std::span<const std::byte> packet, const Parser& parser) {
    if (parser.l2Protocol() != Parser::L2Protocol::Ipv4) {
        return true;
    }

    const auto* ip = parser.ipv4();
    if (ip == nullptr) {
        return true;
    }

    const auto headerBytes = static_cast<std::size_t>(ip->ihl) * 4;
    const auto l2Offset    = parser.l2Offset();
    if (headerBytes < kIpv4MinHeaderBytes || l2Offset + headerBytes > packet.size()) {
        return false;
    }

    const auto* bytes = reinterpret_cast<const std::uint8_t*>(ip);
    std::uint32_t sum{0};
    for (std::size_t i = 0; i + 1 < headerBytes; i += 2) {
        sum += (static_cast<std::uint32_t>(bytes[i]) << 8) | bytes[i + 1];
    }
    while (sum >> 16) {
        sum = (sum & 0xFFFFU) + (sum >> 16);
    }
    return static_cast<std::uint16_t>(sum) == 0xFFFFU;
}

} // namespace vnic::validate
