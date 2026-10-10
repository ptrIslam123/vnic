#pragma once

#include "utils/fifo.h"
#include "utils/allocator.h"

#include <string>
#include <span>
#include <thread>
#include <chrono>

#include <cstdint>
#include <cstring>

#include <iostream>

namespace vnic {

/*
этот класс реализует сущность канала.
Это по сути труба, куда с одной стороны можно класть байты пакетов, с другой стороны Nic будет считывать эти данные
*/
class Link final {
public:
    struct Config {};

    bool configure(const Config& config);
    bool up();
    bool down();
    bool write(std::span<std::byte> data) {
        if (data.empty()) {
            return false;
        }
        // allocate from mem pool to save data
        // memcpy data to allocated memory area
        // make sapn from alocated memory area
        // put this span into fifo

        const auto size{data.size()};
        auto memory{static_cast<std::byte*>(allocator_.allocate(size))};
        if (!memory) {
            return false;
        }

        std::memcpy(memory, data.data(), size);
        return fifo_.push(std::span<std::byte>{memory, size});
    }

    std::size_t receive(
        std::vector<std::byte>& buffer,
        std::chrono::milliseconds timeout = std::chrono::milliseconds{100}
    ) {
        // take from fifo
        // recopy from fifo slot to buffer
        // deallocate memoey are back to memory pool
        auto&& data{fifo_.pop(timeout)};
        if (!data.has_value()) {
            return 0;
        }
        const auto size{data->size()};
        buffer.insert(buffer.end(), data->begin(), data->end());
        allocator_.deallocate(data->data());
        return size;
    }

private:
    Fifo<std::span<std::byte>> fifo_;
    memory::Allocator allocator_;
    Config config_;
};

} // namespace vnic
