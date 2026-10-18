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

/**
 * @file
 * @brief Zero-copy Ethernet frame parser.
 */

namespace vnic {

/**
 * @defgroup group_parser Frame parser
 * @brief Parses Ethernet frames into L1/L2/L3 headers without copying.
 * @{
 */

/**
 * @brief Zero-copy parser for an Ethernet frame.
 *
 * `parse()` walks the frame once and stores non-owning pointers into the input
 * buffer. The parsed view is therefore valid only while the buffer passed to
 * `parse()` stays alive and unchanged.
 *
 * Supported: Ethernet, VLAN (802.1Q / 802.1AD), IPv4, IPv6, ARP and the L4
 * protocols TCP, UDP, ICMP (IPv4) and ICMPv6 (IPv6).
 */
class Parser final {
public:
    /// Layer-1 (link) protocols.
    enum class L1Protocol : std::uint8_t {
        None = 0, ///< Nothing parsed.
        Eth,      ///< Plain Ethernet.
        Vlan,     ///< Ethernet with a VLAN tag.
        Unknown,  ///< Unrecognized L1.
    };

    /// Layer-2 (network) protocols.
    enum class L2Protocol : std::uint8_t {
        None = 0, ///< Nothing parsed.
        Ipv4,     ///< IPv4.
        Ipv6,     ///< IPv6.
        Arp,      ///< ARP.
        Unknown,  ///< Unrecognized L2.
    };

    /// Layer-3 (transport) protocols.
    enum class L3Protocol : std::uint8_t {
        None = 0, ///< Nothing parsed.
        Icmp,     ///< ICMP (IPv4) or ICMPv6 (IPv6).
        Tcp,      ///< TCP.
        Udp,      ///< UDP.
        Unknown,  ///< Unrecognized L3.
    };

    /**
     * @brief Parse an Ethernet frame.
     * @param data Raw frame bytes (without FCS).
     * @return `true` if the frame was parsed; `false` if it is malformed or
     *         truncated.
     */
    [[nodiscard]] bool parse(std::span<const std::byte> data);

    /// @brief L1 protocol of the last successful parse (or `None`).
    L1Protocol l1Protocol() const noexcept;

    /// @brief Ethernet header of the last parse, or `nullptr`.
    const ethhdr* eth() const noexcept;

    /// @brief VLAN ID (0 if the frame carries no VLAN tag).
    std::uint16_t vlanId() const noexcept;

    /// @brief Offset to the L2 header: Eth (14) plus a VLAN tag (4) if present.
    std::size_t l2Offset() const noexcept;

    /// @brief L2 protocol of the last successful parse (or `None`).
    L2Protocol l2Protocol() const noexcept;

    /// @brief IPv4 header of the last parse, or `nullptr`.
    const iphdr* ipv4() const noexcept;

    /// @brief IPv6 header of the last parse, or `nullptr`.
    const ipv6hdr* ipv6() const noexcept;

    /// @brief ARP header of the last parse, or `nullptr`.
    const arphdr* arp() const noexcept;

    /// @brief L3 protocol of the last successful parse (or `None`).
    L3Protocol l3Protocol() const noexcept;

    /// @brief ICMP/ICMPv6 header of the last parse, or `nullptr`.
    const icmphdr* icmp() const noexcept;

    /// @brief TCP header of the last parse, or `nullptr`.
    const tcphdr* tcp() const noexcept;

    /// @brief UDP header of the last parse, or `nullptr`.
    const udphdr* udp() const noexcept;

private:
    bool parseIpv4(std::span<const std::byte> data, std::uint64_t offset);
    bool parseIpv6(std::span<const std::byte> data, std::uint64_t offset);
    bool parseArp(std::span<const std::byte> data, std::uint64_t offset);

    struct L1 {
        L1Protocol protocol = L1Protocol::None;

        // ETH
        const ethhdr* eth{};

        // VLAN
        const std::uint8_t* vlan{}; // pointer to the TCI (2 bytes)
        std::uint16_t vlanId{};     // VLAN ID (12 bits)
        std::uint8_t vlanPcp{};     // Priority (3 bits)
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
    std::size_t l2Offset_{0};
};

/// @brief Print all parsed headers to an output stream (debug helper).
std::ostream& operator<<(std::ostream& os, const Parser& p);

/// @}

} // namespace vnic
