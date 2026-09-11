#pragma once

#include <atomic>
#include <cstdint>

namespace vnic::stats {

class Stats {
public:

private:
    std::uint64_t processedPackets;
    std::uint64_t processedBytes;
    std::uint64_t droppedPackets;
    std::uint64_t droppedBytes;
    std::uint64_t errors;
    std::uint64_t overflows;
};

} // namespace vnic::stats
