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

    bool putRx(std::span<std::byte> packet);
    std::size_t takeRx(std::vector<std::byte>& packet, std::chrono::milliseconds timeout);

    bool putTx(std::span<std::byte> packet);
    std::size_t takeTx(std::vector<std::byte>& packet, std::chrono::milliseconds timeout);

private:
    using Queue = Fifo<std::span<std::byte>>;
    using Allocator = memory::Allocator;

    static bool put(std::span<std::byte> packet, Queue& queue, Allocator& allocator);
    static std::size_t take(std::vector<std::byte>& packet, Queue& queue, Allocator& allocator, std::chrono::milliseconds timeout);

    Queue rxQueue_;
    Queue txQueue_;
    Allocator rxAllocator_;
    Allocator txAllocator_;
    Config config_;
};

} // namespace vnic
