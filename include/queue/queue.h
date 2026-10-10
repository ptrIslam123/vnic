#pragma once

#include "constanst.h"
#include "utils/memory_pool.h"
#include "utils/fifo.h"
#include "utils/state_ful.h"
#include "packet/packet_descriptor.h"
#include "stats/stats.h"

namespace vnic {

class Queue : public utils::Stateful<Queue> {
public:
    struct Config {
        std::uint32_t queueId;           // Идентификатор очереди (0, 1, 2, ...)
        std::uint32_t socketId;          // NUMA-узел для выделения памяти
        std::uint32_t size;              // Количество дескрипторов (размер кольцевого буфера)
    };

    const Config& getConfig() const;
    const stats::Stats& getStats() const;

protected:
    friend utils::Stateful<Queue>;
    using MemPool = memory::Pool<PACKET_BUFFER_SIZE + sizeof(PacketBuffer)>;

    bool configureImpl(const Config& config);
    bool startImpl();
    bool stopImpl();

    Fifo<PacketDescriptor> free_;
    Fifo<PacketDescriptor> used_;
    stats::Stats stats_;
    MemPool memPool_;
    Config config_;
};

} // namespace vnic
