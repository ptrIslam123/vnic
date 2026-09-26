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

    Fifo() {}
    Fifo(const Fifo& ) {}
    Fifo& operator=(const Fifo& ) { return *this; }
    // --- Запись ---
    template<typename... Args>
    bool emplace(Args&&... args);
    bool push(const T& value);
    bool push(T&& value);
    // блокируется до тех пор, пока не сможет записать все
    void pushAll(std::span<const T> data);

    // не блокирующая функция, пытается записать сколько сможет, возвращает сколько смог записать
    std::size_t pushSome(std::span<const T> data);

    // --- Чтение ---
    // бросает исключение если пустое
    T pop();

    // блокируется до тех пор, пока не появится хотя бы один элемент,
    // затем пытается считать сколько сможет, возвращает сколько смог считать
    std::size_t popSome(std::vector<T>& buffer);

    // блокируется до тех пор, пока не появится хотя бы один элемент, но время ожидания ограничена timeout
    // затем пытается считать сколько сможет, возвращает сколько смог считать
    std::size_t popSome(std::vector<T>& buffer, std::chrono::milliseconds timeout);

    template<std::size_t N>
    std::size_t popSome(std::span<T, N> buffer);

    template<std::size_t N>
    std::size_t popSome(std::span<T, N> buffer, std::chrono::milliseconds timeout);

    // не блокирующая функция, считывает сколько есть, возвращает сколько смог считать
    template<std::size_t N>
    std::size_t tryPopSome(std::span<T, N> buffer);

    // --- Управление ---
    // Разблокирует читателей, ожидающих в popSome (используется для выхода
    // из блокирующего RX-патруля при stop). После wake() новые popSome
    // больше НЕ блокируются, пока очередь не опустеет и снова не появится элемент.
    void wake() noexcept;

    // --- Информация ---
    void reserve(std::size_t size);
    bool isEmpty() const;
    std::size_t size() const;

private:
    std::condition_variable condVar_;
    mutable std::mutex mutex_;
    std::queue<T> queue_;
    bool wake_{false};
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
template<std::size_t N>
std::size_t Fifo<T>::popSome(std::span<T, N> buffer, std::chrono::milliseconds timeout) {
    return 0;
}

template<typename T>
template<std::size_t N>
std::size_t Fifo<T>::popSome(std::span<T, N> buffer) {
    std::unique_lock lock(mutex_);

    // Ждём, пока появится хотя бы один элемент (или не придёт wake())
    condVar_.wait(lock, [this] {
        return wake_ || !queue_.empty();
    });

    std::size_t count = 0;
    while (!queue_.empty() && count < buffer.size()) {
        buffer[count] = std::move(queue_.front());
        queue_.pop();
        ++count;
    }
    return count;
}

template<typename T>
std::size_t Fifo<T>::popSome(std::vector<T>& buffer) {
    std::unique_lock lock(mutex_);
    condVar_.wait(lock, [this]{
        return wake_ || !queue_.empty();
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
template<std::size_t N>
std::size_t Fifo<T>::tryPopSome(std::span<T, N> buffer) {
    std::lock_guard lock(mutex_);
    if (queue_.empty()) {
        return 0;
    }

    std::size_t count = 0;
    while (!queue_.empty() && count < buffer.size()) {
        buffer[count] = std::move(queue_.front());
        queue_.pop();
        ++count;
    }
    return count;
}

template<typename T>
void Fifo<T>::wake() noexcept {
    {
        std::lock_guard lock(mutex_);
        wake_ = true;
    }
    condVar_.notify_all();
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
