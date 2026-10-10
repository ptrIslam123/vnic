#pragma once

#include <vector>
#include <string>
#include <span>
#include <array>
#include <cstring>
#include <cstdint>
#include <stdexcept>

inline std::string MacToStr(const std::uint8_t* mac) {
    char buf[18];
    std::snprintf(buf, sizeof(buf), "%02x:%02x:%02x:%02x:%02x:%02x",
                  mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    return buf;
}

inline std::string IpToStr(__be32 addr) {
    in_addr in{};
    in.s_addr = addr;
    return inet_ntoa(in);
}

inline std::uint16_t ComputeChecksum(const void* data, std::size_t len) {
    const auto* ptr = static_cast<const std::uint16_t*>(data);
    std::uint32_t sum = 0;

    while (len > 1) {
        sum += *ptr++;
        len -= 2;
    }

    if (len == 1) {
        sum += *reinterpret_cast<const std::uint8_t*>(ptr);
    }

    while (sum >> 16) {
        sum = (sum & 0xFFFF) + (sum >> 16);
    }

    return static_cast<std::uint16_t>(~sum);
}

inline std::array<std::uint8_t, 6> MacToArray(const std::string& mac) {
    std::array<std::uint8_t, 6> result{};
    unsigned int bytes[6];

    if (std::sscanf(mac.c_str(), "%x:%x:%x:%x:%x:%x",
                    &bytes[0], &bytes[1], &bytes[2],
                    &bytes[3], &bytes[4], &bytes[5]) != 6) {
        throw std::invalid_argument("Invalid MAC: " + mac);
    }

    for (std::size_t i = 0; i < 6; ++i) {
        result[i] = static_cast<std::uint8_t>(bytes[i]);
    }
    return result;
}

inline std::uint32_t IpToU32(const std::string& ip) {
    in_addr addr{};
    if (::inet_pton(AF_INET, ip.c_str(), &addr) != 1) {
        throw std::invalid_argument("Invalid IPv4: " + ip);
    }
    return addr.s_addr;  // уже в сетевом порядке
}

void MakeUdpPacket(
    const std::string& srcMac,
    const std::string& dstMac,
    const std::string& srcIp,
    const std::string& dstIp,
    std::uint16_t srcPort,
    std::uint16_t dstPort,
    std::span<const std::byte> payload,
    std::vector<std::byte>& packet
) {
    // 1. Размеры
    constexpr std::size_t ETH_SIZE = sizeof(ethhdr);   // 14
    constexpr std::size_t IP_SIZE  = sizeof(iphdr);    // 20
    constexpr std::size_t UDP_SIZE = sizeof(udphdr);   // 8

    const std::size_t totalSize = ETH_SIZE + IP_SIZE + UDP_SIZE + payload.size();

    // 2. Выделить буфер
    packet.resize(totalSize);
    std::memset(packet.data(), 0, totalSize);

    auto* base = packet.data();

    //  Ethernet (L2)
    auto* eth = reinterpret_cast<ethhdr*>(base);
    const auto srcMacBytes = MacToArray(srcMac);
    const auto dstMacBytes = MacToArray(dstMac);

    std::memcpy(eth->h_source, srcMacBytes.data(), 6);
    std::memcpy(eth->h_dest,   dstMacBytes.data(), 6);
    eth->h_proto = htons(ETH_P_IP);   // 0x0800

    //  IPv4 (L3)
    auto* ip = reinterpret_cast<iphdr*>(base + ETH_SIZE);

    ip->version  = 4;
    ip->ihl      = IP_SIZE / 4;          // 5
    ip->tos      = 0;
    ip->tot_len  = htons(static_cast<std::uint16_t>(IP_SIZE + UDP_SIZE + payload.size()));
    ip->id       = htons(0);
    ip->frag_off = htons(0);
    ip->ttl      = 64;
    ip->protocol = IPPROTO_UDP;          // 17
    ip->check    = 0;                    // посчитаем ниже
    ip->saddr    = IpToU32(srcIp);
    ip->daddr    = IpToU32(dstIp);

    // IP checksum
    ip->check = ComputeChecksum(ip, IP_SIZE);

    //  UDP (L4)
    auto* udp = reinterpret_cast<udphdr*>(base + ETH_SIZE + IP_SIZE);

    udp->source = htons(srcPort);
    udp->dest   = htons(dstPort);
    udp->len    = htons(static_cast<std::uint16_t>(UDP_SIZE + payload.size()));
    udp->check  = 0;   // 0 = checksum отключён (разрешено для IPv4)

    //  Payload
    if (!payload.empty()) {
        std::memcpy(base + ETH_SIZE + IP_SIZE + UDP_SIZE,
                    payload.data(),
                    payload.size());
    }
}
