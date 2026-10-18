#pragma once

#include <optional>
#include <queue>
#include <mutex>
#include <condition_variable>
#include <vector>
#include <span>
#include <cstddef>
#include <utility>

#include <cstddef>
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
    Fifo() = default;

    Fifo(const Fifo&)            = delete;
    Fifo& operator=(const Fifo&) = delete;

    /// @brief Move constructor.
    Fifo(Fifo&& other) noexcept {
        std::scoped_lock lock{other.mutex_};
        queue_ = std::move(other.queue_);
        wake_  = other.wake_;
    }

    /// @brief Move assignment.
    Fifo& operator=(Fifo&& other) noexcept {
        if (this != &other) {
            std::scoped_lock lock{mutex_, other.mutex_};
            queue_ = std::move(other.queue_);
            wake_  = other.wake_;
        }
        return *this;
    }

    /// @brief Construct an element in place and notify one waiter.
    template <typename... Args> bool emplace(Args&&... args) {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            queue_.emplace(std::forward<Args>(args)...);
        }
        condVar_.notify_one();
        return true;
    }

    /// @brief Push a copy of @p value and notify one waiter.
    bool push(const T& value) {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            queue_.push(value);
        }
        condVar_.notify_one();
        return true;
    }

    /// @brief Push @p value by move and notify one waiter.
    bool push(T&& value) {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            queue_.push(std::move(value));
        }
        condVar_.notify_one();
        return true;
    }

    /// @brief Push all elements of @p data and notify all waiters.
    void pushAll(std::span<const T> data) {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            for (const auto& item : data) {
                queue_.push(item);
            }
        }
        condVar_.notify_all();
    }

    /// @brief Push all elements of @p data; returns the number pushed.
    std::size_t pushSome(std::span<const T> data) {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            for (const auto& item : data) {
                queue_.push(item);
            }
        }
        condVar_.notify_all();
        return data.size();
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

    /// @brief Reserved capacity hint (currently a no-op).
    void reserve(std::size_t) {}

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
    std::condition_variable condVar_;
    mutable std::mutex mutex_;
    std::queue<T> queue_;
    bool wake_{false};
};

} // namespace vnic
