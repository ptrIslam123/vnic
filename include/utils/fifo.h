#pragma once

#include <expected>
#include "error_codes.h"

#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <optional>
#include <queue>
#include <span>
#include <utility>
#include <vector>

#include <cassert>

/**
 * @file
 * @brief Thread-safe FIFO queue.
 */

namespace vnic {

/**
 * @brief A thread-safe FIFO queue of @p T.
 *
 * Production is non-blocking; consumption can either wait on a condition
 * variable with an optional timeout or poll without blocking. The class is
 * move-only.
 *
 * @tparam T Element type.
 * @ingroup group_utils
 */
template <typename T> class Fifo final {
public:
    /**
     * @brief FIFO configuration.
     */
    struct Config {
        std::size_t capacity{0};   ///< Maximum queued elements (`0` = unbounded).
        std::uint32_t socketId{0}; ///< NUMA socket hint (reserved; TODO).

        /// @brief Whether the configuration is valid.
        [[nodiscard]] constexpr bool isValid() const noexcept { return true; }
    };

    Fifo() = default;

    Fifo(const Fifo&)            = delete;
    Fifo& operator=(const Fifo&) = delete;

    /// @brief Move constructor.
    Fifo(Fifo&& other) noexcept {
        std::scoped_lock lock{other.mutex_};
        queue_  = std::move(other.queue_);
        config_ = other.config_;
    }

    /// @brief Move assignment.
    Fifo& operator=(Fifo&& other) noexcept {
        if (this != &other) {
            std::scoped_lock lock{mutex_, other.mutex_};
            queue_  = std::move(other.queue_);
            config_ = other.config_;
        }
        return *this;
    }

    /// @brief Construct an element in place and notify one waiter.
    ///
    /// @return `ErrorCode::QueueFull` if a capacity was set with `reserve()`
    ///         and the queue is already full.
    template <typename... Args> std::expected<void, ErrorCode> emplace(Args&&... args) {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (isFullLocked()) {
                return std::unexpected(ErrorCode::QueueFull);
            }
            queue_.emplace(std::forward<Args>(args)...);
        }
        condVar_.notify_one();
        return {};
    }

    /// @brief Push a copy of @p value and notify one waiter.
    ///
    /// @return `ErrorCode::QueueFull` if a capacity was set with `reserve()`
    ///         and the queue is already full.
    std::expected<void, ErrorCode> push(const T& value) {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (isFullLocked()) {
                return std::unexpected(ErrorCode::QueueFull);
            }
            queue_.push(value);
        }
        condVar_.notify_one();
        return {};
    }

    /// @brief Push @p value by move and notify one waiter.
    ///
    /// @return `ErrorCode::QueueFull` if a capacity was set with `reserve()`
    ///         and the queue is already full.
    std::expected<void, ErrorCode> push(T&& value) {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (isFullLocked()) {
                return std::unexpected(ErrorCode::QueueFull);
            }
            queue_.push(std::move(value));
        }
        condVar_.notify_one();
        return {};
    }

    /// @brief Push all elements of @p data and notify all waiters.
    ///
    /// @return `ErrorCode::QueueFull` if a capacity was set with `reserve()`
    ///         and some elements could not be queued; the elements that fit
    ///         before the queue filled up are still stored.
    std::expected<void, ErrorCode> pushAll(std::span<const T> data) {
        bool full{false};
        {
            std::lock_guard<std::mutex> lock(mutex_);
            for (const auto& item : data) {
                if (isFullLocked()) {
                    full = true;
                    break;
                }
                queue_.push(item);
            }
        }
        condVar_.notify_all();
        if (full) {
            return std::unexpected(ErrorCode::QueueFull);
        }
        return {};
    }

    /// @brief Push all elements of @p data; returns the number pushed.
    std::size_t pushSome(std::span<const T> data) {
        std::size_t pushed{0};
        {
            std::lock_guard<std::mutex> lock(mutex_);
            for (const auto& item : data) {
                if (isFullLocked()) {
                    break;
                }
                queue_.push(item);
                ++pushed;
            }
        }
        condVar_.notify_all();
        return pushed;
    }

    /// @brief Wait up to @p timeout for an element; return it, or `nullopt`.
    std::optional<T> pop(std::chrono::microseconds timeout) {
        std::unique_lock<std::mutex> lock(mutex_);

        // Wait for data with a timeout.
        const bool hasData = condVar_.wait_for(lock, timeout, [this] { return !queue_.empty(); });

        // Timed out: no data.
        if (!hasData) {
            return std::nullopt;
        }

        // Take the element.
        T value = std::move(queue_.front());
        queue_.pop();
        return value;
    }

    /// @brief Block until an element is available and return it.
    T pop() {
        std::unique_lock<std::mutex> lock(mutex_);
        condVar_.wait(lock, [this] { return !queue_.empty(); });
        T value = std::move(queue_.front());
        queue_.pop();
        return value;
    }

    /// @brief Block, then drain the whole queue into @p buffer; return the count.
    std::size_t popSome(std::vector<T>& buffer) {
        std::unique_lock<std::mutex> lock(mutex_);
        condVar_.wait(lock, [this] { return !queue_.empty(); });

        std::size_t count = 0;
        while (!queue_.empty()) {
            buffer.push_back(std::move(queue_.front()));
            queue_.pop();
            ++count;
        }
        return count;
    }

    /// @brief Wait up to @p timeout, then drain the whole queue into @p buffer.
    std::size_t popSome(std::vector<T>& buffer, std::chrono::milliseconds timeout) {
        std::unique_lock<std::mutex> lock(mutex_);
        if (!condVar_.wait_for(lock, timeout, [this] { return !queue_.empty(); })) {
            return 0;
        }

        std::size_t count = 0;
        while (!queue_.empty()) {
            buffer.push_back(std::move(queue_.front()));
            queue_.pop();
            ++count;
        }
        return count;
    }

    /// @brief Block, then pop up to @p N elements into @p buffer.
    template <std::size_t N> std::size_t popSome(std::span<T, N> buffer) {
        std::unique_lock<std::mutex> lock(mutex_);
        condVar_.wait(lock, [this] { return !queue_.empty(); });

        std::size_t count = 0;
        while (!queue_.empty() && count < N) {
            buffer[count++] = std::move(queue_.front());
            queue_.pop();
        }
        return count;
    }

    /// @brief Wait up to @p timeout, then pop up to @p N elements into @p buffer.
    template <std::size_t N> std::size_t popSome(std::span<T, N> buffer, std::chrono::milliseconds timeout) {
        std::unique_lock<std::mutex> lock(mutex_);
        if (!condVar_.wait_for(lock, timeout, [this] { return !queue_.empty(); })) {
            return 0;
        }

        std::size_t count = 0;
        while (!queue_.empty() && count < N) {
            buffer[count++] = std::move(queue_.front());
            queue_.pop();
        }
        return count;
    }

    /// @brief Pop up to @p N elements without blocking.
    template <std::size_t N> std::size_t tryPopSome(std::span<T, N> buffer) {
        std::lock_guard<std::mutex> lock(mutex_);
        std::size_t count = 0;
        while (!queue_.empty() && count < N) {
            buffer[count++] = std::move(queue_.front());
            queue_.pop();
        }
        return count;
    }

    /// @brief Apply @p config to the queue.
    ///
    /// The capacity is a logical reservation: the FIFO does not pre-allocate
    /// storage, but subsequent pushes fail once the queue reaches it.
    ///
    /// @return `ErrorCode::ConfigQueueInvalid` if the configuration is invalid.
    std::expected<void, ErrorCode> configure(const Config& config) {
        if (!config.isValid()) {
            return std::unexpected(ErrorCode::ConfigQueueInvalid);
        }
        std::lock_guard<std::mutex> lock{mutex_};
        config_ = config;
        return {};
    }

    /// @brief Current configuration.
    ///
    /// Configure the queue before sharing it between threads.
    [[nodiscard]] const Config& getConfig() const noexcept { return config_; }

    /// @brief Set the maximum number of queued elements (`0` = unbounded).
    ///
    /// This is a logical reservation: the FIFO does not pre-allocate storage,
    /// but subsequent pushes fail once the queue reaches @p capacity.
    void reserve(std::size_t capacity) {
        std::lock_guard<std::mutex> lock{mutex_};
        config_.capacity = capacity;
    }

    /// @brief Maximum number of queued elements (`0` = unbounded).
    std::size_t capacity() const {
        std::lock_guard<std::mutex> lock{mutex_};
        return config_.capacity;
    }

    /// @brief NUMA socket hint (reserved; not used by the implementation yet).
    [[nodiscard]] std::uint32_t socketId() const {
        std::lock_guard<std::mutex> lock{mutex_};
        return config_.socketId;
    }

    /// @brief Whether the queue is empty.
    bool isEmpty() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return queue_.empty();
    }

    /// @brief Number of elements currently queued.
    std::size_t size() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return queue_.size();
    }

private:
    bool isFullLocked() const noexcept { return config_.capacity != 0 && queue_.size() >= config_.capacity; }

    std::condition_variable condVar_;
    mutable std::mutex mutex_;
    std::queue<T> queue_;
    Config config_;
};

} // namespace vnic
