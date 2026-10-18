#pragma once

#include <cstdlib>
#include <cstdint>

/**
 * @file
 * @brief Fixed-size block pool.
 */

namespace vnic::memory {

/**
 * @defgroup group_memory Memory
 * @brief Buffer pool and variable-size allocator abstractions.
 * @{
 */

/**
 * @brief A pool of fixed-size blocks.
 *
 * @tparam N Block size in bytes returned by `allocate()`.
 *
 * @note Version 0.1.0 is a stub backed by `malloc`/`free`. A pre-allocating,
 *       NUMA-aware pool is planned.
 */
template <std::size_t N> class Pool final {
public:
    /// Pool configuration.
    struct Config {
        std::uint32_t blockSize{0}; ///< Block size hint (reserved).
        std::uint32_t capacity{0};  ///< Number of blocks to pre-allocate (reserved).
        std::uint32_t socketId{0};  ///< NUMA socket hint (reserved).
    };

    /// @brief Apply configuration (currently just stores it).
    bool configure(const Config& config) {
        config_ = config;
        return true;
    }

    /// @brief Allocate one block of @p N bytes; `nullptr` on failure.
    void* allocate() { return malloc(N); }

    /// @brief Release a block obtained from `allocate()`.
    void deallocate(void* ptr) { free(ptr); }

private:
    Config config_;
};

/// @}

} // namespace vnic::memory
