#include "link/link.h"

#include <cstring>

namespace vnic {

bool Link::put(std::span<std::byte> packet, Queue& queue, Allocator& allocator) {
    if (packet.empty()) {
        return false;
    }

    const auto size{packet.size()};
    auto memory{static_cast<std::byte*>(allocator.allocate(size))};
    if (!memory) {
        return false;
    }

    std::memcpy(memory, packet.data(), size);
    if (!queue.push(std::span<std::byte>{memory, size})) [[unlikely]] {
        allocator.deallocate(memory);
        return false;
    }
    return true;
}

std::size_t Link::take(std::vector<std::byte>& packet, Queue& queue, Allocator& allocator,
                       std::chrono::milliseconds timeout) {
    auto&& data{queue.pop(timeout)};
    if (!data.has_value()) {
        return 0;
    }
    const auto size{data->size()};
    packet.insert(packet.end(), data->begin(), data->end());
    allocator.deallocate(data->data());
    return size;
}

bool Link::putRx(std::span<std::byte> packet) { return put(packet, rxQueue_, rxAllocator_); }

std::size_t Link::takeRx(std::vector<std::byte>& packet, std::chrono::milliseconds timeout) {
    return take(packet, rxQueue_, rxAllocator_, timeout);
}

bool Link::putTx(std::span<std::byte> packet) { return put(packet, txQueue_, txAllocator_); }

std::size_t Link::takeTx(std::vector<std::byte>& packet, std::chrono::milliseconds timeout) {
    return take(packet, txQueue_, txAllocator_, timeout);
}

bool Link::configure(const Config& config) {
    config_ = config;
    return true;
}

bool Link::up() {
    up_ = true;
    return true;
}

bool Link::down() {
    up_ = false;
    return true;
}

bool Link::isUp() const noexcept { return up_; }

} // namespace vnic
