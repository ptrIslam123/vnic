#pragma once

#include <cstdlib>
#include <cstdint>

namespace vnic::memory {

class Allocator final {
public:
    struct Config {
        std::uint32_t blockSize;
        std::uint32_t capacity;
        std::uint32_t socketId;
    };
    bool configure(const Config& config) {
        config_ = config;
        return true;
    }
    void* allocate(std::size_t size) {
        return malloc(size);
    }
    void deallocate(void* ptr) {
        free(ptr);
    }

private:
    Config config_;
};

} // namespace vnic::memory