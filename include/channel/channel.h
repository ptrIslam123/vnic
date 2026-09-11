#pragma once

#include "clock/clock.h"
#include "queue/fifo.h"

#include <thread>
#include <span>
#include <vector>
#include <cstdint>

namespace vnic::soft {

template<typename T, typename Sizeof>
class Channel {
public:
    struct Config {
        enum class Duplex : std::uint8_t {
            Full, Half
        } duplex;
        bool autoNegotiation;  // Включено ли автосогласование
        std::uint32_t speedMbps;  // Скорость в Мбит/с
    };

    bool configure(const Config& config);
    bool up();
    bool down();

    void put(std::span<const T> data) {
        std::size_t written = 0;

        while (written < data.size()) {
            const auto now = clock::monotonic::Now();
            const auto dtNs = now - lastWriteTime_;  // наносекунды

            // Сколько байт можем записать за dtNs?
            const auto bytesAllowed = (speedBytesPerSecond_ * dtNs) / 1'000'000'000;

            auto rest = data.subspan(written);

            // Сколько элементов влезает в bytesAllowed?
            std::size_t elementsAllowed = 0;
            std::size_t accumulater = 0;
            for (const auto& d : rest) {
                const auto elemSize = Sizeof::operator()(d);
                if (accumulater + elemSize > bytesAllowed) {
                    break;
                }
                accumulater += elemSize;
                ++elementsAllowed;
            }

            // Если ничего не влезает — ждём
            if (elementsAllowed == 0) {
                const auto elemSize = Sizeof::operator()(rest.front());
                const auto waitNs = (elemSize * 1'000'000'000) / speedBytesPerSecond_;
                std::this_thread::sleep_for(std::chrono::nanoseconds(waitNs));
                continue;  // ← lastWriteTime_ НЕ обновляем!
            }

            // Записываем в очередь
            written += fifo_.enqueue({rest.begin(), rest.begin() + elementsAllowed});

            // Сдвигаем lastWriteTime_ на время, потраченное на запись
            const auto timeSpentNs = (accumulater * 1'000'000'000) / speedBytesPerSecond_;
            lastWriteTime_ += timeSpentNs;
        }
    }

    void get(std::vector<T>& buffer) {
        buffer.clear();
        // просто считываем все данные в очереди
    }

private:
    std::uint32_t speedBytesPerSecond_;
    Fifo<T> fifo_;
    std::uint64_t lastWriteTime_;
    Config config_;
};

} // namespace vnic::soft
