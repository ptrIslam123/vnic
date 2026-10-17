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

namespace vnic {

template<typename T>
class Fifo final {
public:

    Fifo() = default;

    Fifo(const Fifo&) = delete;
    Fifo& operator=(const Fifo&) = delete;

    Fifo(Fifo&& other) noexcept {
        std::scoped_lock lock{other.mutex_};
        queue_ = std::move(other.queue_);
        wake_ = other.wake_;
    }

    Fifo& operator=(Fifo&& other) noexcept {
        if (this != &other) {
            std::scoped_lock lock{mutex_, other.mutex_};
            queue_ = std::move(other.queue_);
            wake_ = other.wake_;
        }
        return *this;
    }

    template<typename... Args>
    bool emplace(Args&&... args) {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            queue_.emplace(std::forward<Args>(args)...);
        }
        condVar_.notify_one();
        return true;
    }

    bool push(const T& value) {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            queue_.push(value);
        }
        condVar_.notify_one();
        return true;
    }

    bool push(T&& value) {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            queue_.push(std::move(value));
        }
        condVar_.notify_one();
        return true;
    }

    void pushAll(std::span<const T> data) {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            for (const auto& item : data) {
                queue_.push(item);
            }
        }
        condVar_.notify_all();
    }

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

    std::optional<T> pop(std::chrono::microseconds timeout) {
        std::unique_lock<std::mutex> lock(mutex_);

        // Ждём данные с таймаутом
        const bool hasData = condVar_.wait_for(lock, timeout, [this] {
            return !queue_.empty();
        });

        // Таймаут — данных нет
        if (!hasData) {
            return std::nullopt;
        }

        // Забрать элемент
        T value = std::move(queue_.front());
        queue_.pop();
        return value;
    }

    T pop() {
        std::unique_lock<std::mutex> lock(mutex_);
        condVar_.wait(lock, [this] { return !queue_.empty(); });
        T value = std::move(queue_.front());
        queue_.pop();
        return value;
    }

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

    template<std::size_t N>
    std::size_t popSome(std::span<T, N> buffer) {
        std::unique_lock<std::mutex> lock(mutex_);
        condVar_.wait(lock, [this] { return !queue_.empty(); });

        std::size_t count = 0;
        while (!queue_.empty() && count < N) {
            buffer[count++] = std::move(queue_.front());
            queue_.pop();
        }
        return count;
    }

    template<std::size_t N>
    std::size_t popSome(std::span<T, N> buffer, std::chrono::milliseconds timeout) {
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

    template<std::size_t N>
    std::size_t tryPopSome(std::span<T, N> buffer) {
        std::lock_guard<std::mutex> lock(mutex_);
        std::size_t count = 0;
        while (!queue_.empty() && count < N) {
            buffer[count++] = std::move(queue_.front());
            queue_.pop();
        }
        return count;
    }

    void reserve(std::size_t) {}

    bool isEmpty() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return queue_.empty();
    }

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
