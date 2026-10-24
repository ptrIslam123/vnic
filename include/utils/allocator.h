#pragma once

#include <expected>
#include "error_codes.h"

#include <cstdint>
#include <cstdlib>

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
 * @note Version 0.1.x is a stub backed by `malloc`/`free`.
 * @ingroup group_memory
 */
class Allocator final {
public:
    /// Allocator configuration (reserved).
    struct Config {
        std::uint32_t blockSize{0}; ///< Block size hint (reserved).
        std::uint32_t capacity{0};  ///< Capacity hint (reserved).
        std::uint32_t socketId{0};  ///< NUMA socket hint (reserved).

        /// @brief Validate the configuration before it is applied.
        [[nodiscard]] bool isValid() const noexcept { return true; }
    };

    /// @brief Apply configuration (currently just stores it).
    std::expected<void, ErrorCode> configure(const Config& config) {
        if (!config.isValid()) {
            return std::unexpected(ErrorCode::ConfigAllocatorInvalid);
        }
        config_ = config;
        return {};
    }

    /// @brief Allocate @p size bytes.
    /// @return `ErrorCode::AllocatorInvalidSize` for a zero size,
    ///         `ErrorCode::AllocatorAllocationFailed` if `malloc` fails.
    std::expected<void*, ErrorCode> allocate(std::size_t size) {
        if (size == 0) {
            return std::unexpected(ErrorCode::AllocatorInvalidSize);
        }
        void* ptr = std::malloc(size);
        if (ptr == nullptr) [[unlikely]] {
            return std::unexpected(ErrorCode::AllocatorAllocationFailed);
        }
        return ptr;
    }

    /// @brief Release a buffer obtained from `allocate()`.
    void deallocate(void* ptr) { std::free(ptr); }

private:
    Config config_;
};

} // namespace vnic::memory
