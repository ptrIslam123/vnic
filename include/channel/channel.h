#pragma once

#include "clock/clock.h"
#include "queue/fifo.h"

#include <atomic>
#include <thread>
#include <span>
#include <vector>

#include <cstdint>
#include <cassert>

namespace vnic::soft {

// TODO: Это пока не оптимальная и не lock_free реализация
template<typename T, typename Sizeof>
class Channel {
public:
    struct Config {
        enum class Duplex : std::uint8_t {
            Full, Half
        } duplex = Duplex::Full;
        bool autoNegotiation;
        std::uint32_t speedMbps;
    };

    bool configure(const Config& config) {
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

    bool up() {
        up_.store(true);
        return true;
    }

    bool down() {
        up_.store(false);
        return true;
    }

    bool isUp() const {
        return up_.load();
    }

    void put(const T& data) {
        put(std::span<const T>{&data, 1});
    }

    void put(std::span<const T> data) {
        std::size_t written = 0;

        while (written < data.size()) {
            assert(isUp());

            const auto now = clock::monotonic::Now();
            const auto dtNs = now - lastWriteTime_;  // наносекунды

            // Сколько байт можем записать за dtNs?
            const auto bytesAllowed = static_cast<std::uint64_t>(speedBytesPerSecond_ * dtNs / 1'000'000'000);

            auto rest = data.subspan(written);

            // Сколько элементов влезает в bytesAllowed?
            std::size_t elementsAllowed = 0;
            std::size_t accumulated = 0;
            for (const auto& d : rest) {
                const auto elemSize = Sizeof{}.operator()(d);
                if (accumulated + elemSize > bytesAllowed) {
                    break;
                }
                accumulated += elemSize;
                ++elementsAllowed;
            }

            // Если ничего не влезает — ждём
            if (elementsAllowed == 0) {
                const auto elemSize = Sizeof{}.operator()(rest.front());
                const auto waitNs = (elemSize * 1'000'000'000) / speedBytesPerSecond_;
                std::this_thread::sleep_for(std::chrono::nanoseconds(waitNs));
                continue;  // lastWriteTime_ НЕ обновляем!
            }


            // Записываем в очередь
            const auto enqueued = fifo_.pushSome(std::span{rest.begin(), rest.begin() + elementsAllowed});
            written += enqueued;

            // Сдвигаем lastWriteTime_ на время, потраченное на запись
            const auto timeSpentNs = (accumulated * 1'000'000'000) / speedBytesPerSecond_;
            lastWriteTime_ += timeSpentNs;
        }
    }

    std::size_t get(std::vector<T>& buffer) {
        return fifo_.popSome(buffer);
    }

private:
    std::uint64_t speedBytesPerSecond_ = 0;
    std::uint64_t lastWriteTime_ = 0;

    Fifo<T> fifo_;

    std::atomic<bool> up_;
    Config config_;
};

} // namespace vnic::soft