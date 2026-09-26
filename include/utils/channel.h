#pragma once

#include "constanst.h"
#include "memory/memory_pool.h"
#include "packet/packet_buffer.h"
#include "clock/clock.h"
#include "utils/fifo.h"

#include <atomic>
#include <thread>
#include <span>
#include <vector>
#include <array>

#include <cstring>
#include <cstdint>
#include <cassert>

namespace vnic {

class Channel {
public:
    struct Config {
        enum class Duplex : std::uint8_t {
            Full, Half
        } duplex = Duplex::Full;
        bool autoNegotiation;
        std::uint32_t speedMbps;

        std::uint64_t size;
        memory::Pool::Config memConfig;
    };

    bool configure(const Config& config) {
        config_ = config;

        if (!allocator_.configure(config_.memConfig)) [[unlikely]] {
            return false;
        }

        if (config.speedMbps == 0) {
            return false;
        }

        config_ = config;

        // Мбит/с → байт/с
        speedBytesPerSecond_ = static_cast<std::uint64_t>(config_.speedMbps * 1'000'000 / 8);

        // Half duplex → делим пропускную способность пополам
        if (config_.duplex == Config::Duplex::Half) {
            speedBytesPerSecond_ /= 2;
        }

        return true;
    }

    bool put(std::span<const std::byte> data) {
        // Ограничение пропускной способности
        if (!waitForBandwidth(data.size())) {
            return false;
        }

        auto needSegments{(data.size() + PACKET_BUFFER_SIZE - 1) / PACKET_BUFFER_SIZE};
        std::array<PacketBuffer, MAX_SEGMENTS> buffers;
        std::uint64_t written{0};

        while (needSegments > 0) {
            const auto n{free_.popSome(std::span{buffers.data(), needSegments})};
            if (n == 0) {
                std::this_thread::sleep_for(std::chrono::microseconds{100});
                continue;
            }

            for (std::uint64_t i{0}; i < n; ++i) {
                const auto remaining{data.size() - written};
                const auto size{std::min<std::size_t>(PACKET_BUFFER_SIZE, remaining)};

                std::memcpy(buffers[i].memory, data.data() + written, size);
                buffers[i].length = static_cast<std::uint32_t>(size);
                written += size;
            }

            used_.pushAll(std::span{buffers.data(), n});
            needSegments -= n;
        }
        return true;
    }

    std::size_t take(std::vector<PacketBuffer>& buffers, std::chrono::milliseconds timeout) {
        return used_.popSome(buffers, timeout);
    }

    void free(std::span<PacketBuffer> buffer) {
        free_.pushAll(buffer);
    }

    bool up() {
        for (std::uint64_t i{0}; i < config_.size; ++i) {
            auto memory{allocator_.allocate(PACKET_BUFFER_SIZE)};
            if (!memory) [[unlikely]] {
                return false;
            }
            PacketBuffer buffer{.memory = static_cast<std::uint8_t*>(memory)};
            free_.push(buffer);
        }
        return true;
    }

    bool down() {
        while (!free_.isEmpty()) {
            auto&& buffer{free_.pop()};
            if (!buffer.memory) [[unlikely]] {
                assert(buffer.memory);
                continue;
            }
            allocator_.deallocate(buffer.memory);
        }

        while (!used_.isEmpty()) {
            auto&& buffer{free_.pop()};
            if (!buffer.memory) [[unlikely]] {
                assert(buffer.memory);
                continue;
            }
            allocator_.deallocate(buffer.memory);
        }
        return true;
    }

private:
    bool waitForBandwidth(std::size_t bytes) {
        while (true) {
            const auto now = clock::monotonic::Now();
            const auto dtNs = now - lastWriteTime_;

            // Сколько байт можно записать за dtNs?
            const auto bytesAllowed = (speedBytesPerSecond_ * dtNs) / 1'000'000'000;

            if (bytesAllowed >= bytes) {
                // Хватает — обновляем lastWriteTime
                lastWriteTime_ = now;
                return true;
            }

            // Не хватает — ждём
            const auto bytesNeeded = bytes - bytesAllowed;
            const auto waitNs = (bytesNeeded * 1'000'000'000) / speedBytesPerSecond_;
            std::this_thread::sleep_for(std::chrono::nanoseconds{waitNs});
        }
    }

    std::uint64_t speedBytesPerSecond_ = 0;
    std::uint64_t lastWriteTime_ = 0;

    Fifo<PacketBuffer> free_;
    Fifo<PacketBuffer> used_;
    memory::Pool allocator_;

    Config config_;
};

} // namespace vnic
