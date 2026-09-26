#pragma once

#include <cstdint>

namespace vnic::memory {

class Pool final {
public:
    struct Config {
        std::uint32_t blockSize;
        std::uint32_t capacity;
        std::uint32_t socketId;
    };
    bool configure(const Config& config);
    void* allocate(std::size_t size);
    void deallocate(void* ptr);

private:
    Config config_;
};

} // namespace vnic::memory
