#pragma once

#include "memory/memory_pool.h"
#include "queue/fifo.h"
#include "packet/packet.h"
#include "stats/stats.h"

namespace vnic {

class Queue final {
public:
    struct Config {
        std::uint32_t queueId;           // Идентификатор очереди (0, 1, 2, ...)
        std::uint32_t socketId;          // NUMA-узел для выделения памяти
        std::uint32_t size;              // Количество дескрипторов (размер кольцевого буфера)
    };

    Queue() {};
    Queue(const Queue& other) {}
    Queue& operator=(const Queue& other) {
        return *this;
    }
    bool configure(const Config& config);
    bool start();
    bool stop();
    void push(Packet&& packet);

    const Config& getConfig() const;
    const stats::Stats& getStats() const;

private:
    void notifyRxListener();

    Fifo<PacketDescriptor> free_;
    Fifo<PacketDescriptor> used_;
    stats::Stats stats_;
    memory::Pool memory_;
    Config config_;
};

class RxQueue final {
public:
    struct Config : Queue::Config {};

    bool configure(const Config& config) { return queue_.configure(config); }
    bool start() { return queue_.start(); }
    bool stop() { return queue_.stop(); }
    void push(Packet&& packet) { return queue_.push(std::move(packet)); }

    const stats::Stats& getStats() const { return queue_.getStats(); }

private:
    Queue queue_;
};

class TxQueue final {
public:
    struct Config : Queue::Config {};

    bool configure(const Config& config) { return queue_.configure(config); }
    bool start() { return queue_.start(); }
    bool stop() { return queue_.stop(); }
    void push(Packet&& packet) {
        //!TODO
    }

    const stats::Stats& getStats() const { return queue_.getStats(); }

private:
    Queue queue_;
};

} // namespace vnic
