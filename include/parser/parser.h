#pragma once

#include <expected>
#include "error_codes.h"

#include <array>
#include <optional>
#include <ostream>
#include <span>
#include <string>
#include <string_view>

#include <cstddef>
#include <cstdint>

#include <linux/icmp.h>
#include <linux/if_ether.h>
#include <linux/ip.h>
#include <linux/ipv6.h>
#include <linux/tcp.h>
#include <linux/udp.h>
#include <net/if_arp.h>
#include <netinet/in.h>

/**
 * @file
 * @brief Ethernet frame parser.
 */

namespace vnic {

/**
 * @defgroup group_parser Frame parser
 * @brief Parses Ethernet frames into header values.
 * @{
 */

/**
 * @brief Parser for an Ethernet frame.
 *
 * `parse()` walks the frame once and copies the fixed L1/L2/L3 header fields
 * into the parser. Payload bytes are not copied. Accessors return optional
 * header values, so the result is valid even after the input buffer is
 * released and never exposes a misaligned pointer into the frame.
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
     * @brief Reset the parser to its initial (unparsed) state.
     *
     * Clears every stored header and protocol back to `None`. Called
     * automatically at the start of `parse()`, so a parser can be reused
     * across frames without leaking state from a previous frame.
     */
    void reset() noexcept;

    /**
     * @brief Parse an Ethernet frame.
     *
     * Resets the parser first, then walks the frame once. On failure the
     * parser is left holding only what was parsed up to the failure point.
     *
     * @param data Raw frame bytes (without FCS).
     * @return An empty `std::expected` on success; otherwise the specific
     *         `ErrorCode` describing why the frame is malformed or truncated.
     */
    [[nodiscard]] std::expected<void, ErrorCode> parse(std::span<const std::byte> data);

    /// @brief L1 protocol of the last successful parse (or `None`).
    L1Protocol l1Protocol() const noexcept;

    /// @brief Ethernet header of the last parse, or `std::nullopt`.
    std::optional<ethhdr> eth() const noexcept;

    /// @brief VLAN ID (0 if the frame carries no VLAN tag).
    std::uint16_t vlanId() const noexcept;

    /// @brief Offset to the L2 header: Eth (14) plus a VLAN tag (4) if present.
    std::size_t l2Offset() const noexcept;

    /// @brief L2 protocol of the last successful parse (or `None`).
    L2Protocol l2Protocol() const noexcept;

    /// @brief IPv4 header of the last parse, or `std::nullopt`.
    std::optional<iphdr> ipv4() const noexcept;

    /// @brief IPv6 header of the last parse, or `std::nullopt`.
    std::optional<ipv6hdr> ipv6() const noexcept;

    /// @brief ARP header of the last parse, or `std::nullopt`.
    std::optional<arphdr> arp() const noexcept;

    /// @brief L3 protocol of the last successful parse (or `None`).
    L3Protocol l3Protocol() const noexcept;

    /// @brief ICMP/ICMPv6 header of the last parse, or `std::nullopt`.
    std::optional<icmphdr> icmp() const noexcept;

    /// @brief TCP header of the last parse, or `std::nullopt`.
    std::optional<tcphdr> tcp() const noexcept;

    /// @brief UDP header of the last parse, or `std::nullopt`.
    std::optional<udphdr> udp() const noexcept;

private:
    std::expected<void, ErrorCode> parseIpv4(std::span<const std::byte> data, std::uint64_t offset);
    std::expected<void, ErrorCode> parseIpv6(std::span<const std::byte> data, std::uint64_t offset);
    std::expected<void, ErrorCode> parseArp(std::span<const std::byte> data, std::uint64_t offset);

    friend std::ostream& operator<<(std::ostream& os, const Parser& p);

    struct L1 {
        L1Protocol protocol = L1Protocol::None; ///< Parsed L1 protocol.

        std::optional<ethhdr> eth; ///< Ethernet header, if present.

        std::uint16_t vlanId{}; ///< VLAN ID (12 bits).
        std::uint8_t vlanPcp{}; ///< VLAN priority (3 bits).
    };

    struct L2 {
        L2Protocol protocol = L2Protocol::None; ///< Parsed L2 protocol.
        std::optional<iphdr> ip;                ///< IPv4 header, if present.
        std::optional<ipv6hdr> ip6;             ///< IPv6 header, if present.
        std::optional<arphdr> arp;              ///< ARP header, if present.
    };

    struct L3 {
        L3Protocol protocol = L3Protocol::None; ///< Parsed L3 protocol.
        std::optional<icmphdr> icmp;            ///< ICMP/ICMPv6 header, if present.
        std::optional<tcphdr> tcp;              ///< TCP header, if present.
        std::optional<udphdr> udp;              ///< UDP header, if present.
    };

    L1 l1_;
    L2 l2_;
    L3 l3_;
    std::size_t l2Offset_{0};

    // ARP hardware/protocol addresses (only valid for hlen=6, plen=4).
    std::array<std::uint8_t, 6> arpSenderHw_{};
    std::array<std::uint8_t, 6> arpTargetHw_{};
    std::uint32_t arpSenderProto_{};
    std::uint32_t arpTargetProto_{};
    bool hasArpAddresses_{false};
};

/// @brief Print all parsed headers to an output stream (debug helper).
std::ostream& operator<<(std::ostream& os, const Parser& p);

/**
 * @brief Format a MAC address as `aa:bb:cc:dd:ee:ff`.
 *
 * @param mac Six address bytes in network order.
 * @return Lower-case colon-separated representation.
 * @ingroup group_parser
 */
std::string MacToString(std::span<const std::uint8_t, 6> mac);

/**
 * @brief Format an IPv4 address in dotted-decimal notation.
 *
 * @param addr Address in network byte order, as stored in `iphdr::saddr`.
 * @return Textual representation (for example `"192.168.1.1"`).
 * @ingroup group_parser
 */
std::string Ipv4ToString(std::uint32_t addr);

/**
 * @brief Format an IPv6 address in compressed notation.
 *
 * @param addr Sixteen address bytes in network order.
 * @return Textual representation as produced by `inet_ntop` (for example
 *         `"2001:db8::1"`).
 * @ingroup group_parser
 */
std::string Ipv6ToString(std::span<const std::uint8_t, 16> addr);

/**
 * @brief Human-readable name of an L1 protocol.
 *
 * @param p Protocol value from the last parse.
 * @return Static name; unrecognized values yield `"Unknown"`.
 * @ingroup group_parser
 */
std::string_view L1Name(Parser::L1Protocol p);

/**
 * @brief Human-readable name of an L2 protocol.
 *
 * @param p Protocol value from the last parse.
 * @return Static name; unrecognized values yield `"Unknown"`.
 * @ingroup group_parser
 */
std::string_view L2Name(Parser::L2Protocol p);

/**
 * @brief Human-readable name of an L3 protocol.
 *
 * @param p Protocol value from the last parse.
 * @return Static name; unrecognized values yield `"Unknown"`.
 * @ingroup group_parser
 */
std::string_view L3Name(Parser::L3Protocol p);

/**
 * @brief Human-readable name of an ARP operation.
 *
 * @param op Operation code in network byte order, as stored in
 *           `arphdr::ar_op`.
 * @return Static name such as `"request"` or `"reply"`; unrecognized values
 *         yield `"?"`.
 * @ingroup group_parser
 */
std::string_view ArpOpName(std::uint16_t op);

/**
 * @brief Human-readable name of an ICMP/ICMPv6 type.
 *
 * @param type Type value in host byte order, as stored in `icmphdr::type`.
 * @return Static name such as `"echo-request"`; unrecognized values yield
 *         `"?"`.
 * @ingroup group_parser
 */
std::string_view IcmpTypeName(std::uint8_t type);

} // namespace vnic
