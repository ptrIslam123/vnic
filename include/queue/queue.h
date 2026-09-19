#pragma once

#include "memory/memory_pool.h"
#include "utils/fifo.h"
#include "utils/state_ful.h"
#include "packet/packet.h"
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

    bool configureImpl(const Config& config);
    bool startImpl();
    bool stopImpl();

    void notifyRxListener();

    Fifo<PacketDescriptor> free_;
    Fifo<PacketDescriptor> used_;
    stats::Stats stats_;
    memory::Pool memory_;
    Config config_;
};

} // namespace vnic
