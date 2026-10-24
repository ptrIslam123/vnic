#include "link/link.h"
#include "utils/scoped_guard.h"

#include <cstring>
#include <new>

namespace vnic {

std::expected<void, ErrorCode> Link::put(std::span<std::byte> packet, Queue& queue, Allocator& allocator,
                                         ErrorCode fullError) {
    if (packet.empty()) {
        return std::unexpected(ErrorCode::LinkPacketEmpty);
    }

    const auto size{packet.size()};
    auto memoryResult{allocator.allocate(size)};
    if (!memoryResult) [[unlikely]] {
        return std::unexpected(memoryResult.error());
    }

    auto* memory{static_cast<std::byte*>(*memoryResult)};
    std::memcpy(memory, packet.data(), size);
    utils::ScopedGuard guard{[&] { allocator.deallocate(memory); }};
    if (auto pushed = queue.push(std::span<std::byte>{memory, size}); !pushed) [[unlikely]] {
        return std::unexpected(fullError);
    }
    guard.cancel();
    return {};
}

std::expected<std::size_t, ErrorCode> Link::take(std::vector<std::byte>& packet, Queue& queue, Allocator& allocator,
                                                 std::chrono::milliseconds timeout) {
    auto&& data{queue.pop(timeout)};
    if (!data.has_value()) {
        return std::unexpected(ErrorCode::Timeout);
    }
    const auto size{data->size()};
    utils::ScopedGuard guard{[&] { allocator.deallocate(data->data()); }};
    packet.insert(packet.end(), data->begin(), data->end());
    return size;
}

std::expected<void, ErrorCode> Link::putRx(std::span<std::byte> packet) {
    return put(packet, rxQueue_, rxAllocator_, ErrorCode::LinkRxQueueFull);
}

std::expected<std::size_t, ErrorCode> Link::takeRx(std::vector<std::byte>& packet, std::chrono::milliseconds timeout) {
    try {
        return take(packet, rxQueue_, rxAllocator_, timeout);
    } catch (const std::bad_alloc&) {
        return std::unexpected(ErrorCode::LinkAllocationFailed);
    }
}

std::expected<void, ErrorCode> Link::putTx(std::span<std::byte> packet) {
    return put(packet, txQueue_, txAllocator_, ErrorCode::LinkTxQueueFull);
}

std::expected<std::size_t, ErrorCode> Link::takeTx(std::vector<std::byte>& packet, std::chrono::milliseconds timeout) {
    try {
        return take(packet, txQueue_, txAllocator_, timeout);
    } catch (const std::bad_alloc&) {
        return std::unexpected(ErrorCode::LinkAllocationFailed);
    }
}

std::expected<void, ErrorCode> Link::configure(const Config& linkConfig) {
    if (const auto code = linkConfig.validate(); code != ErrorCode::None) {
        return std::unexpected(code);
    }

    const memory::Allocator::Config rxAllocatorConfig{.blockSize = linkConfig.rxFrameSize,
                                                      .capacity  = linkConfig.rxQueueDepth};
    const memory::Allocator::Config txAllocatorConfig{.blockSize = linkConfig.txFrameSize,
                                                      .capacity  = linkConfig.txQueueDepth};

    if (auto result = rxAllocator_.configure(rxAllocatorConfig); !result) [[unlikely]] {
        return result;
    }
    if (auto result = txAllocator_.configure(txAllocatorConfig); !result) [[unlikely]] {
        return result;
    }

    config_ = linkConfig;
    rxQueue_.reserve(config_.rxQueueDepth);
    txQueue_.reserve(config_.txQueueDepth);
    return {};
}

std::expected<void, ErrorCode> Link::up() {
    up_.store(true, std::memory_order_release);
    return {};
}

std::expected<void, ErrorCode> Link::down() {
    up_.store(false, std::memory_order_release);
    return {};
}

bool Link::isUp() const noexcept { return up_.load(std::memory_order_acquire); }

Link::~Link() {
    drain(rxQueue_, rxAllocator_);
    drain(txQueue_, txAllocator_);
}

void Link::drain(Queue& queue, Allocator& allocator) noexcept {
    while (true) {
        auto frame{queue.pop(std::chrono::microseconds{0})};
        if (!frame.has_value()) {
            return;
        }
        allocator.deallocate(frame->data());
    }
}

} // namespace vnic
