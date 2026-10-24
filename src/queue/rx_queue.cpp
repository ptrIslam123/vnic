#include "queue/rx_queue.h"
#include "constants.h"
#include "packet/packet_buffer.h"
#include "utils/scoped_guard.h"

#include <array>
#include <chrono>
#include <limits>

#include <cstdint>

namespace vnic {

bool RxQueue::Config::isValid() const noexcept { return size > 0; }

std::expected<void, ErrorCode> RxQueue::put(PacketDescriptor&& desc, std::span<const std::byte> packet) {
    const auto bytes{packet.size()};
    if (bytes == 0) [[unlikely]] {
        return std::unexpected(ErrorCode::QueueZeroLengthPacket);
    }
    if (bytes > std::numeric_limits<std::uint32_t>::max()) [[unlikely]] {
        return std::unexpected(ErrorCode::QueuePacketTooLarge);
    }

    const auto segments{static_cast<std::uint64_t>((bytes + kPacketBufferSize - 1) / kPacketBufferSize)};
    if (segments > kMaxSegments) [[unlikely]] {
        stats_.fetchOverflows();
        return std::unexpected(ErrorCode::QueueSegmentLimitExceeded);
    }

    // Выделяем все сегменты, не связывая
    static thread_local std::array<PacketBuffer*, kMaxSegments> buffers{};
    std::uint64_t bufferIdx{0};
    utils::ScopedGuard buffersGuard{[&] {
        for (std::uint64_t j{0}; j < bufferIdx; ++j)
            memPool_.deallocate(buffers[j]);
    }};
    for (; bufferIdx < segments; ++bufferIdx) {
        auto block{memPool_.allocate()};
        if (!block) [[unlikely]] {
            return std::unexpected(block.error());
        }
        buffers[bufferIdx] = static_cast<PacketBuffer*>(*block);
    }

    // Заполняем заголовки и связываем
    std::size_t offset{0};
    for (std::uint64_t segmentIdx{0}; segmentIdx < segments; ++segmentIdx) {
        auto buffer{buffers[segmentIdx]};
        const auto chunk{std::min<std::size_t>(kPacketBufferSize, bytes - offset)};
        buffer->length = static_cast<std::uint32_t>(chunk);
        buffer->next   = (segmentIdx + 1 < segments) ? buffers[segmentIdx + 1] : nullptr;
        std::memcpy(reinterpret_cast<std::byte*>(buffer + 1), packet.data() + offset, chunk);
        offset += chunk;
    }
    desc.buffer      = buffers[0];
    desc.totalLength = static_cast<std::uint32_t>(bytes);

    auto slot{free_.pop(std::chrono::microseconds{0})};
    if (!slot.has_value()) [[unlikely]] {
        stats_.fetchOverflows();
        return std::unexpected(ErrorCode::QueueFreeListEmpty);
    }
    utils::ScopedGuard slotGuard{[&] { free_.push(std::move(*slot)); }};

    if (auto pushed = used_.push(std::move(desc)); !pushed) [[unlikely]] {
        return std::unexpected(pushed.error() == ErrorCode::QueueFull ? ErrorCode::QueueUsedListFull : pushed.error());
    }

    slotGuard.cancel();
    stats_.fetchProcessedPackets();
    stats_.fetchProcessedBytes(bytes);
    buffersGuard.cancel();
    return {};
}

// Забирает из used_ - очереди все накопленные дескрипторы (не блокируется),
// возвращает количество изъятых дескрипторов.
std::uint64_t RxQueue::take(std::vector<PacketDescriptor>& descs) {
    static thread_local std::array<PacketDescriptor, kMaxSegments> storage{};
    const auto n{used_.tryPopSome(std::span{storage})};
    descs.insert(descs.cend(), storage.cbegin(), storage.cbegin() + n);
    return n;
}

// Возвращает дескрипторы обратно в пул свободных и освобождает ВСЮ цепочку
// буферов пакета (все сегменты через поле next, а не только первый).
std::expected<void, ErrorCode> RxQueue::free(std::span<const PacketDescriptor> descs) {
    for (const PacketDescriptor& desc : descs) {
        releaseChain(desc.buffer);
    }
    if (auto pushed = free_.pushAll(descs); !pushed) [[unlikely]] {
        return std::unexpected(pushed.error() == ErrorCode::QueueFull ? ErrorCode::QueueFreeListFull : pushed.error());
    }
    return {};
}

void RxQueue::releaseChain(PacketBuffer* head) noexcept {
    for (PacketBuffer* segment{head}; segment != nullptr;) {
        PacketBuffer* next = segment->next;
        memPool_.deallocate(segment);
        segment = next;
    }
}

std::expected<void, ErrorCode> RxQueue::configureImpl(const Config& config) {
    if (!config.isValid()) {
        return std::unexpected(ErrorCode::ConfigQueueSizeInvalid);
    }
    config_ = config;

    const MemPool::Config memConfig{
        .blockSize = kPacketBufferSize, .capacity = config_.size, .socketId = config_.socketId};
    if (auto result = memPool_.configure(memConfig); !result) [[unlikely]] {
        return result;
    }

    const Fifo<PacketDescriptor>::Config fifoConfig{.capacity = config_.size, .socketId = config_.socketId};
    if (auto result = free_.configure(fifoConfig); !result) [[unlikely]] {
        return result;
    }
    if (auto result = used_.configure(fifoConfig); !result) [[unlikely]] {
        return result;
    }

    return {};
}

std::expected<void, ErrorCode> RxQueue::start() {
    if (getState() == State::Stopped) {
        if (auto result = configure(config_); !result) {
            return result;
        }
    }
    return utils::Stateful<RxQueue>::start();
}

std::expected<void, ErrorCode> RxQueue::startImpl() {
    utils::ScopedGuard cleanup{[this] {
        while (!free_.isEmpty()) {
            (void)free_.pop();
        }
        while (!used_.isEmpty()) {
            releaseChain(used_.pop().buffer);
        }
    }};

    for (decltype(config_.size) i{0}; i < config_.size; ++i) {
        PacketDescriptor desc;
        if (auto result = free_.push(std::move(desc)); !result) [[unlikely]] {
            return result;
        }
    }

    cleanup.cancel();
    return {};
}

std::expected<void, ErrorCode> RxQueue::stopImpl() {
    while (!free_.isEmpty()) {
        (void)free_.pop();
    }

    while (!used_.isEmpty()) {
        releaseChain(used_.pop().buffer);
    }

    return {};
}

RxQueue::~RxQueue() { (void)stopImpl(); }

const RxQueue::Config& RxQueue::getConfig() const { return config_; }
const stats::Stats& RxQueue::getStats() const { return stats_; }

} // namespace vnic
