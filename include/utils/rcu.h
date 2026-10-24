#pragma once

#include <mutex>
#include <shared_mutex>
#include <utility>

/**
 * @file
 * @brief Minimal RCU-style guard for a rarely-updated shared value.
 */

namespace vnic::utils {

/**
 * @brief A guard around a value that is read often and written rarely.
 *
 * Backed by a `std::shared_mutex`: readers take a shared lock, the writer
 * takes a unique lock. Used for the RSS redirection table, which is read on
 * every received frame and updated only on `VNic::updateReta()`.
 *
 * @warning This is the temporary implementation. Taking a shared lock on every
 *          received frame is not free.
 * @todo Replace the internals with a lock-free reclamation scheme while keeping
 *       the same interface (`read`/`snapshot`/`write`). For the current usage
 *       (rare writer, single reader RX thread) a single-reader RCU with a pool
 *       of pre-allocated buffers and deferred reclamation, hazard pointers or a
 *       seqlock would fit.
 *
 * @tparam T Value type.
 * @ingroup group_utils
 */
template <typename T> class Rcu final {
public:
    Rcu() = default;

    /// @brief Construct with an initial value.
    explicit Rcu(T value) : value_{std::move(value)} {}

    Rcu(const Rcu&)            = delete;
    Rcu& operator=(const Rcu&) = delete;

    /**
     * @brief Read the value without copying it as a whole.
     *
     * Calls @p fn with a `const T&` under the shared lock. The result is
     * returned by value, so no reference to the internal value escapes.
     *
     * @param fn Callable invoked with the value.
     * @return Whatever @p fn returns.
     */
    template <typename F> auto read(F&& fn) const {
        std::shared_lock lock{mutex_};
        return fn(value_);
    }

    /// @brief Copy of the current value (control-plane use).
    T snapshot() const {
        std::shared_lock lock{mutex_};
        return value_;
    }

    /// @brief Publish a new value (replaces the old one atomically).
    void write(T&& value) {
        std::unique_lock lock{mutex_};
        value_ = std::move(value);
    }

    /// @brief Publish a new value (replaces the old one atomically).
    void write(const T& value) {
        std::unique_lock lock{mutex_};
        value_ = value;
    }

private:
    mutable std::shared_mutex mutex_;
    T value_{};
};

} // namespace vnic::utils
