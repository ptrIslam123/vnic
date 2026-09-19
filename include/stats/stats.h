#pragma once

#include <cstdint>
#include <type_traits>

namespace vnic::stats {

// Счётчики статистики NIC и отдельных очередей.
//
// Класс должен оставаться копируемым (используется и как член VNic,
// и как член Queue, и возвращается по значению из getStats()), поэтому
// члены — обычные std::uint64_t, а НЕ std::atomic<uint64_t>.
// Атомарность обеспечивается на уровне операций через __atomic_* builtins
// (GCC/Clang): чтение — __atomic_load_n, инкремент — __atomic_fetch_add,
// сброс — __atomic_store_n. Для чистых счётчиков достаточно relaxed-порядка,
// т.к. упорядочивание памяти между потоками не требуется.
class Stats {
public:
    // --- Чтение (атомарный load) ---
    std::uint64_t loadProcessedPackets() const { return __atomic_load_n(&processedPackets_, __ATOMIC_RELAXED); }
    std::uint64_t loadProcessedBytes() const { return __atomic_load_n(&processedBytes_, __ATOMIC_RELAXED); }
    std::uint64_t loadDroppedPackets() const { return __atomic_load_n(&droppedPackets_, __ATOMIC_RELAXED); }
    std::uint64_t loadDroppedBytes() const { return __atomic_load_n(&droppedBytes_, __ATOMIC_RELAXED); }
    std::uint64_t loadErrors() const { return __atomic_load_n(&errors_, __ATOMIC_RELAXED); }
    std::uint64_t loadOverflows() const { return __atomic_load_n(&overflows_, __ATOMIC_RELAXED); }

    // --- Инкремент (атомарный fetch_add) ---
    void fetchProcessedPackets(std::uint64_t n = 1) { __atomic_fetch_add(&processedPackets_, n, __ATOMIC_RELAXED); }
    void fetchProcessedBytes(std::uint64_t n = 1) { __atomic_fetch_add(&processedBytes_, n, __ATOMIC_RELAXED); }
    void fetchDroppedPackets(std::uint64_t n = 1) { __atomic_fetch_add(&droppedPackets_, n, __ATOMIC_RELAXED); }
    void fetchDroppedBytes(std::uint64_t n = 1) { __atomic_fetch_add(&droppedBytes_, n, __ATOMIC_RELAXED); }
    void fetchErrors(std::uint64_t n = 1) { __atomic_fetch_add(&errors_, n, __ATOMIC_RELAXED); }
    void fetchOverflows(std::uint64_t n = 1) { __atomic_fetch_add(&overflows_, n, __ATOMIC_RELAXED); }

    // --- Сброс в 0 (атомарный store) ---
    void reset() {
        __atomic_store_n(&processedPackets_, 0, __ATOMIC_RELAXED);
        __atomic_store_n(&processedBytes_, 0, __ATOMIC_RELAXED);
        __atomic_store_n(&droppedPackets_, 0, __ATOMIC_RELAXED);
        __atomic_store_n(&droppedBytes_, 0, __ATOMIC_RELAXED);
        __atomic_store_n(&errors_, 0, __ATOMIC_RELAXED);
        __atomic_store_n(&overflows_, 0, __ATOMIC_RELAXED);
    }

private:
    std::uint64_t processedPackets_{0};
    std::uint64_t processedBytes_{0};
    std::uint64_t droppedPackets_{0};
    std::uint64_t droppedBytes_{0};
    std::uint64_t errors_{0};
    std::uint64_t overflows_{0};
};

} // namespace vnic::stats