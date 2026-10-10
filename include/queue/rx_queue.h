#pragma once

#include "constanst.h"
#include "utils/memory_pool.h"
#include "utils/fifo.h"
#include "utils/state_ful.h"
#include "stats/stats.h"
#include "packet/packet_descriptor.h"

#include <array>
#include <algorithm>
#include <span>
#include <vector>

#include <cstring>
#include <cassert>
#include <cstdint>

namespace vnic {

class RxQueue final : public utils::Stateful<RxQueue> {
public:
    struct Config {
        std::uint16_t queueId;
        std::uint32_t size;
        std::uint32_t socketId;
    };

    bool put(PacketDescriptor&& desc, std::span<const std::byte> packet);
    std::uint64_t take(std::vector<PacketDescriptor>& descs);
    bool free(std::span<const PacketDescriptor> descs);

    const Config& getConfig() const;
    const stats::Stats& getStats() const;

private:
    friend utils::Stateful<RxQueue>;
    using MemPool = memory::Pool<PACKET_BUFFER_SIZE + sizeof(PacketBuffer)>;

    bool configureImpl(const Config& config);
    bool startImpl();
    bool stopImpl();

    Fifo<PacketDescriptor> free_;
    Fifo<PacketDescriptor> used_;
    MemPool memPool_;
    stats::Stats stats_;
    Config config_;
};

} // namespace vnic