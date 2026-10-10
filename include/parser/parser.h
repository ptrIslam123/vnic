#pragma once

#include <ostream>
#include <cstdint>
#include <cstddef>
#include <span>

struct ethhdr;

struct iphdr;
struct ipv6hdr;
struct arphdr;

struct icmphdr;
struct tcphdr;
struct udphdr;

namespace vnic {

class Parser final {
public:
    enum class L1Protocol : std::uint8_t {
        None = 0,
        Eth,
        Vlan,
        Unknown,
    };

    enum class L2Protocol : std::uint8_t {
        None = 0,
        Ipv4,
        Ipv6,
        Arp,
        Unknown,
    };

    enum class L3Protocol : std::uint8_t {
        None = 0,
        Icmp,
        Tcp,
        Udp,
        Unknown,
    };

    [[nodiscard]] bool parse(std::span<const std::byte> data);

    L1Protocol l1Protocol() const noexcept;
    const ethhdr* eth() const noexcept;

    L2Protocol l2Protocol() const noexcept;
    const iphdr* ipv4() const noexcept;
    const ipv6hdr* ipv6() const noexcept;
    const arphdr* arp() const noexcept;

    L3Protocol l3Protocol() const noexcept;
    const icmphdr* icmp() const noexcept;
    const tcphdr* tcp() const noexcept;
    const udphdr* udp() const noexcept;

private:
    bool parseIpv4(std::span<const std::byte> data, std::uint64_t offset);
    bool parseArp(std::span<const std::byte> data, std::uint64_t offset);

    struct L1 {
        L1Protocol protocol = L1Protocol::None;

        // ETH
        const ethhdr* eth{};

        // VLAN
        const std::uint8_t* vlan{}; // указатель на TCI (2 байта)
        std::uint16_t vlanId{}; // VLAN ID (12 бит)
        std::uint8_t vlanPcp{}; // Priority (3 бита)
    };

    struct L2 {
        L2Protocol protocol = L2Protocol::None;
        union {
            const iphdr* ip;
            const ipv6hdr* ip6;
            const arphdr* arp;
        } hdr;
    };

    struct L3 {
        L3Protocol protocol = L3Protocol::None;
        union {
            const icmphdr* icmp;
            const tcphdr* tcp;
            const udphdr* udp;
        } hdr;
    };

    L1 l1_;
    L2 l2_;
    L3 l3_;
};

std::ostream& operator<<(std::ostream& os, const Parser& p);

} // namespace vnic