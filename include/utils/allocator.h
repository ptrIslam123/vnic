#pragma once

#include <cstdlib>
#include <cstdint>

/**
 * @file
 * @brief Variable-size allocator.
 */

namespace vnic::memory {

/**
 * @brief An allocator for variable-size buffers.
 *
 * Used by the @ref Link to copy frames in and out.
 *
 * @note Version 0.1.0 is a stub backed by `malloc`/`free`.
 * @ingroup group_memory
 */
class Allocator final {
public:
    /// Allocator configuration (reserved).
    struct Config {
        std::uint32_t blockSize{0}; ///< Block size hint (reserved).
        std::uint32_t capacity{0};  ///< Capacity hint (reserved).
        std::uint32_t socketId{0};  ///< NUMA socket hint (reserved).
    };

    /// @brief Apply configuration (currently just stores it).
    bool configure(const Config& config) {
        config_ = config;
        return true;
    }

    /// @brief Allocate @p size bytes; `nullptr` on failure.
    void* allocate(std::size_t size) { return malloc(size); }

    /// @brief Release a buffer obtained from `allocate()`.
    void deallocate(void* ptr) { free(ptr); }

private:
    Config config_;
};

} // namespace vnic::memory
