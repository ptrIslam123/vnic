#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace vnic {

class Parser;

namespace validate {

// Разбирает MAC-адрес формата "aa:bb:cc:dd:ee:ff". При ошибке — nullopt.
std::optional<std::array<std::uint8_t, 6>> ParseMac(const std::string& mac);

// Набор независимых проверок кадра, которые выполняет аппаратный NIC на приёме.
// Каждая функция проверяет ровно одно условие и возвращает true, если кадр его
// проходит. VNic::validate вызывает их в фиксированном порядке.

// Проверка размера кадра: не короче minFrameSize (runts) и не длиннее
// l2Offset + mtu. MTU ограничивает L3-нагрузку, а не полный кадр, поэтому к ней
// добавляется заголовок канального уровня (включая возможный VLAN-тег).
bool CheckFrameSize(std::span<const std::byte> packet, const Parser& parser,
                    std::size_t minFrameSize, std::uint32_t mtu);

// Контрольная последовательность кадра (FCS/CRC32). В текущей архитектуре Link
// передаёт байты без FCS, поэтому проверка всегда проходит (слот под будущее).
bool CheckFcs(std::span<const std::byte> packet);

// Фильтр MAC-адреса получателя: свой unicast, broadcast или multicast.
// При promiscuousEnabled фильтр отключён.
bool CheckDstMac(const Parser& parser,
                 const std::array<std::uint8_t, 6>& macAddr,
                 bool promiscuousEnabled);

// VLAN-фильтр: VLAN ID должен входить в allowList. Пустой список = выключен.
bool CheckVlanFilter(const Parser& parser, const std::vector<std::uint16_t>& allowList);

// IPv4 total length: поле заголовка не должно превышать фактический размер кадра.
bool CheckIpTotalLength(std::span<const std::byte> packet, const Parser& parser);

// Контрольная сумма IPv4-заголовка (только заголовок, не payload).
bool CheckIpv4Checksum(std::span<const std::byte> packet, const Parser& parser);

} // namespace validate
} // namespace vnic
