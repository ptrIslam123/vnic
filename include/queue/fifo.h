#pragma once

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
    // --- Запись ---
    template<typename... Args>
    void emplace(Args&&... args);
    void push(const T& value);
    void push(T&& value);
    void pushAll(std::span<const T> data);
    std::size_t pushSome(std::span<const T> data);

    // --- Чтение ---
    T pop();
    std::size_t popSome(std::vector<T>& buffer);

    // --- Информация ---
    void reserve(std::size_t size);
    bool isEmpty() const;
    std::size_t size() const;

private:
    std::condition_variable condVar_;
    mutable std::mutex mutex_;
    std::queue<T> queue_;
};

template<typename T>
template<typename... Args>
void Fifo<T>::emplace(Args&&... args) {
    std::lock_guard lock(mutex_);
    queue_.emplace(std::forward<Args>(args)...);
    condVar_.notify_one();
}

template<typename T>
void Fifo<T>::push(const T& value) {
    {
        std::lock_guard lock(mutex_);
        queue_.push(value);
    }
    condVar_.notify_one();
}

template<typename T>
void Fifo<T>::push(T&& value) {
    {
        std::lock_guard lock(mutex_);
        queue_.push(std::move(value));
    }
    condVar_.notify_one();
}

template<typename T>
void Fifo<T>::pushAll(std::span<const T> data) {
    {
        std::lock_guard lock(mutex_);
        for (const auto& item : data) {
            queue_.push(item);
        }
    }
    condVar_.notify_one();
}

template<typename T>
std::size_t Fifo<T>::pushSome(std::span<const T> data) {
    std::size_t count = 0;
    {
        std::lock_guard lock(mutex_);
        for (const auto& item : data) {
            queue_.push(item);
            ++count;
        }
    }
    condVar_.notify_one();
    return count;
}

template<typename T>
T Fifo<T>::pop() {
    std::unique_lock lock(mutex_);
    condVar_.wait(lock, [this]{
        return !queue_.empty();
    });

    assert(!queue_.empty());
    T value = std::move(queue_.front());
    queue_.pop();
    return value;
}

template<typename T>
std::size_t Fifo<T>::popSome(std::vector<T>& buffer) {
    std::unique_lock lock(mutex_);
    condVar_.wait(lock, [this]{
        return !queue_.empty();
    });

    std::size_t count = 0;
    while (!queue_.empty()) {
        buffer.push_back(std::move(queue_.front()));
        queue_.pop();
        ++count;
    }
    return count;
}

template<typename T>
void Fifo<T>::reserve(std::size_t size) {
    std::lock_guard lock(mutex_);
    // std::queue не поддерживает reserve напрямую,
    (void)size;
}

template<typename T>
bool Fifo<T>::isEmpty() const {
    std::lock_guard lock(mutex_);
    return queue_.empty();
}

template<typename T>
std::size_t Fifo<T>::size() const {
    std::lock_guard lock(mutex_);
    return queue_.size();
}

} // namespace vnic