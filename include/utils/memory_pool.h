#pragma once

#include <cstdlib>
#include <cstdint>

namespace vnic::memory {

template<std::size_t N>
class Pool final {
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
    void* allocate() {
        return malloc(N);
    }
    void deallocate(void* ptr) {
        free(ptr);
    }

private:
    Config config_;
};

} // namespace vnic::memory
