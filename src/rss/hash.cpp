#include "rss/hash.h"

namespace vnic::rss::soft {

std::uint32_t calc_hash(const vnic::Packet& packet, std::span<const std::uint8_t> key, HashFunc hf, Protocol protocol) {
    return 0; //! TODO
}

} // namespace vnic::rss::soft
