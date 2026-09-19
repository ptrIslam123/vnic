#include "queue/queue.h"

#include <cstring>
#include <cassert>

namespace vnic {

bool Queue::configure(const Config& config) {
    config_ = config;

    const memory::Pool::Config memConfig{
        .blockSize = Packet::SEGMENT_SIZE,
        .capacity  = config_.size,
        .socketId  = config_.socketId
    };
    if (!memory_.configure(memConfig)) [[unlikely]] {
        return false;
    }

    return true;
}

bool Queue::start() {
    free_.reserve(config_.size);
    used_.reserve(config_.size);

    for (decltype(config_.size) i{0}; i < config_.size; ++i) {
        auto buffer{memory_.allocate(Packet::SEGMENT_SIZE)};
        if (!buffer) [[unlikely]] {
            return false;
        }

        PacketDescriptor desc;
        desc.data = buffer;
        free_.push(std::move(desc));
    }

    return true;
}

bool Queue::stop() {
    while (!free_.isEmpty()) {
        auto&& desc{free_.pop()};
        memory_.deallocate(desc.data);
    }

    while (!used_.isEmpty()) {
        auto&& desc{used_.pop()};
        memory_.deallocate(desc.data);
    }

    return true;
}

void Queue::push(Packet&& packet) {
    // Packet - это цепочка сегментов (next). Это ЧАСТИ ОДНОГО пакета,
    // а не отдельные пакеты. Их нельзя передать по частям — иначе
    // приложение получит только часть данных.
    //
    // Поэтому: сначала считаем, сколько нужно дескрипторов,
    // проверяем, что места хватит на ВСЕ, и только потом перемещаем.

    // Подсчёт сегментов
    std::size_t segments = 0;
    std::size_t bytes = 0;

    packet.forEachSegment([&](const Packet& p) {
        ++segments;
        bytes += p.length;
        return true;
    });

    // Если места меньше, чем сегментов - дропаем весь сетевой пакет
    if (free_.size() < segments) [[unlikely]] {
        // stats_.overflows++;
        // stats_.droppedPackets += segments;
        // stats_.droppedBytes += bytes;
        return;
    }

    // Перемещение сетевого пакет в очередь
    packet.forEachSegment([&](Packet& p) {
        auto descr = free_.pop();
        descr.setMetadata(p);
        std::memcpy(descr.data, p.data, p.length);
        used_.push(std::move(descr));
        return true;
    });

    notifyRxListener();
}

void Queue::notifyRxListener() {
    // TODO
}

const Queue::Config& Queue::getConfig() const { return config_; }
const stats::Stats& Queue::getStats() const { return stats_; }

} // namespace vnic
