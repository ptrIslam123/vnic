#include "memory/memory_pool.h"

#include <cstdlib>

namespace vnic::memory {

bool Pool::configure(const Config& config) {
    config_ = config;
    return true;
}

void* Pool::allocate(std::size_t size) {
    return malloc(size); //! TODO
}

void Pool::deallocate(void* ptr) {
    return free(ptr); //! TODO
}

} // namespace vnic::memory