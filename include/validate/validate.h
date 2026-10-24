#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

/**
 * @file
 * @brief Receive-time frame checks performed by the port.
 */

namespace vnic {

class Parser;

/**
 * @defgroup group_validate Receive validation
 * @brief Independent checks a hardware NIC performs when receiving a frame.
 * @{
 */

namespace validate {

/**
 * @brief Parse a MAC address of the form `"aa:bb:cc:dd:ee:ff"`.
 * @param mac MAC address string.
 * @return The six octets, or `std::nullopt` if @p mac is malformed.
 */
std::optional<std::array<std::uint8_t, 6>> ParseMac(const std::string& mac);

// Each function below checks exactly one condition and returns true when the
// frame passes. VNic::validate() calls them in a fixed order.

/**
 * @brief Check the frame size (runts and MTU).
 * @param packet      Raw frame.
 * @param parser      Parser positioned on @p packet.
 * @param minFrameSize Minimum accepted size (runts).
 * @param mtu         Maximum L3 payload; the parser's L2 offset is added to it
 *                    before comparing.
 * @return `true` if the frame is within `[minFrameSize, l2Offset + mtu]`.
 */
bool CheckFrameSize(std::span<const std::byte> packet, const Parser& parser, std::size_t minFrameSize,
                    std::uint32_t mtu);

/**
 * @brief Check the frame check sequence (FCS/CRC32).
 * @return Always `true` in the current architecture (the @ref vnic::Link does
 *         not carry an FCS); a placeholder for the future.
 */
bool CheckFcs(std::span<const std::byte> packet);

/**
 * @brief Destination-MAC filter (own unicast, broadcast or multicast).
 * @param parser             Parser positioned on the frame.
 * @param macAddr            The port's MAC address.
 * @param promiscuousEnabled When `true` the filter is disabled.
 * @return `true` if the frame passes.
 */
bool CheckDstMac(const Parser& parser, const std::array<std::uint8_t, 6>& macAddr, bool promiscuousEnabled);

/**
 * @brief VLAN filter.
 * @param parser    Parser positioned on the frame.
 * @param allowList Allowed VLAN IDs; an empty list disables the filter.
 * @return `true` if the frame's VLAN ID is allowed.
 */
bool CheckVlanFilter(const Parser& parser, const std::vector<std::uint16_t>& allowList);

/**
 * @brief IPv4 total-length sanity check.
 * @return `true` if the header total-length field does not exceed the actual
 *         frame size.
 */
bool CheckIpTotalLength(std::span<const std::byte> packet, const Parser& parser);

/**
 * @brief IPv4 header checksum check (header only, not payload).
 * @return `true` if the checksum is valid.
 */
bool CheckIpv4Checksum(std::span<const std::byte> packet, const Parser& parser);

} // namespace validate

/// @}

} // namespace vnic
