#pragma once

#include <expected>
#include "error_codes.h"

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstdlib>

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
 * @note Version 0.1.x is a stub backed by `malloc`/`free`. A pre-allocating,
 *       NUMA-aware pool is planned.
 */
template <std::size_t N> class Pool final {
public:
    /// Pool configuration.
    struct Config {
        std::uint32_t blockSize{0}; ///< Block size hint (reserved).
        std::uint32_t capacity{0};  ///< Number of blocks to pre-allocate (reserved).
        std::uint32_t socketId{0};  ///< NUMA socket hint (reserved).

        /// @brief Validate the configuration before it is applied.
        [[nodiscard]] bool isValid() const noexcept { return capacity > 0; }
    };

    Pool() = default;

    Pool(const Pool&)            = delete;
    Pool& operator=(const Pool&) = delete;

    /// @brief Move the configuration and allocation counters.
    Pool(Pool&& other) noexcept :
        config_(other.config_), live_(other.live_.load(std::memory_order_relaxed)),
        total_(other.total_.load(std::memory_order_relaxed)) {}

    /// @brief Move-assign the configuration and allocation counters.
    Pool& operator=(Pool&& other) noexcept {
        if (this != &other) {
            config_ = other.config_;
            live_.store(other.live_.load(std::memory_order_relaxed), std::memory_order_relaxed);
            total_.store(other.total_.load(std::memory_order_relaxed), std::memory_order_relaxed);
        }
        return *this;
    }

    /// @brief Apply configuration (currently just stores it).
    std::expected<void, ErrorCode> configure(const Config& config) {
        if (!config.isValid()) {
            return std::unexpected(ErrorCode::ConfigPoolInvalid);
        }
        config_ = config;
        return {};
    }

    /// @brief Allocate one block of @p N bytes.
    /// @return `ErrorCode::MemoryPoolExhausted` if `malloc` fails.
    std::expected<void*, ErrorCode> allocate() {
        void* ptr = std::malloc(N);
        if (ptr == nullptr) [[unlikely]] {
            return std::unexpected(ErrorCode::MemoryPoolExhausted);
        }
        live_.fetch_add(1, std::memory_order_relaxed);
        total_.fetch_add(1, std::memory_order_relaxed);
        return ptr;
    }

    /// @brief Release a block obtained from `allocate()`.
    void deallocate(void* ptr) {
        if (ptr != nullptr) {
            live_.fetch_sub(1, std::memory_order_relaxed);
        }
        std::free(ptr);
    }

    /// @brief Last applied configuration.
    const Config& getConfig() const noexcept { return config_; }

    /// @brief Number of blocks currently allocated and not yet released.
    std::size_t liveBlocks() const noexcept { return live_.load(std::memory_order_relaxed); }

    /// @brief Number of blocks ever allocated since construction.
    std::size_t totalBlocks() const noexcept { return total_.load(std::memory_order_relaxed); }

private:
    Config config_;
    std::atomic<std::size_t> live_{0};
    std::atomic<std::size_t> total_{0};
};

/// @}

} // namespace vnic::memory
