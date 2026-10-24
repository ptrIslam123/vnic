#pragma once

#include <cstdint>
#include <type_traits>

/**
 * @file
 * @brief Atomic packet counters.
 */

namespace vnic::stats {

/**
 * @defgroup group_stats Statistics
 * @brief Counter set shared by the port and each queue.
 * @{
 */

/**
 * @brief A set of packet/byte counters.
 *
 * The class stays copyable (it is embedded in `VNic` and in each queue and is
 * returned by value from `getStats()`), so its members are plain
 * `std::uint64_t` rather than `std::atomic`. Atomicity is provided per
 * operation through the `__atomic_*` builtins (GCC/Clang): reads use
 * `__atomic_load_n`, increments `__atomic_fetch_add`, reset `__atomic_store_n`.
 * Relaxed ordering is sufficient for pure counters because no cross-thread
 * memory ordering is required.
 *
 * Each queue keeps its own object, so a single event is not double-counted.
 */
class Stats {
public:
    /// @name Reads (atomic load)
    /// @{

    /// @brief Number of successfully processed (received) packets.
    std::uint64_t loadProcessedPackets() const { return __atomic_load_n(&processedPackets_, __ATOMIC_RELAXED); }
    /// @brief Number of successfully processed bytes.
    std::uint64_t loadProcessedBytes() const { return __atomic_load_n(&processedBytes_, __ATOMIC_RELAXED); }
    /// @brief Number of dropped packets.
    std::uint64_t loadDroppedPackets() const { return __atomic_load_n(&droppedPackets_, __ATOMIC_RELAXED); }
    /// @brief Number of dropped bytes.
    std::uint64_t loadDroppedBytes() const { return __atomic_load_n(&droppedBytes_, __ATOMIC_RELAXED); }
    /// @brief Number of errors.
    std::uint64_t loadErrors() const { return __atomic_load_n(&errors_, __ATOMIC_RELAXED); }
    /// @brief Number of queue overflows.
    std::uint64_t loadOverflows() const { return __atomic_load_n(&overflows_, __ATOMIC_RELAXED); }
    /// @brief Number of transmitted packets.
    std::uint64_t loadTransmittedPackets() const { return __atomic_load_n(&transmittedPackets_, __ATOMIC_RELAXED); }
    /// @brief Number of transmitted bytes.
    std::uint64_t loadTransmittedBytes() const { return __atomic_load_n(&transmittedBytes_, __ATOMIC_RELAXED); }
    /// @brief Number of packets dropped on transmit.
    std::uint64_t loadTxDroppedPackets() const { return __atomic_load_n(&txDroppedPackets_, __ATOMIC_RELAXED); }
    /// @brief Number of bytes dropped on transmit.
    std::uint64_t loadTxDroppedBytes() const { return __atomic_load_n(&txDroppedBytes_, __ATOMIC_RELAXED); }
    /// @}

    /// @name Increments (atomic fetch_add)
    /// @{

    /// @brief Add @p n to the processed-packets counter.
    void fetchProcessedPackets(std::uint64_t n = 1) { __atomic_fetch_add(&processedPackets_, n, __ATOMIC_RELAXED); }
    /// @brief Add @p n to the processed-bytes counter.
    void fetchProcessedBytes(std::uint64_t n = 1) { __atomic_fetch_add(&processedBytes_, n, __ATOMIC_RELAXED); }
    /// @brief Add @p n to the dropped-packets counter.
    void fetchDroppedPackets(std::uint64_t n = 1) { __atomic_fetch_add(&droppedPackets_, n, __ATOMIC_RELAXED); }
    /// @brief Add @p n to the dropped-bytes counter.
    void fetchDroppedBytes(std::uint64_t n = 1) { __atomic_fetch_add(&droppedBytes_, n, __ATOMIC_RELAXED); }
    /// @brief Add @p n to the errors counter.
    void fetchErrors(std::uint64_t n = 1) { __atomic_fetch_add(&errors_, n, __ATOMIC_RELAXED); }
    /// @brief Add @p n to the overflows counter.
    void fetchOverflows(std::uint64_t n = 1) { __atomic_fetch_add(&overflows_, n, __ATOMIC_RELAXED); }
    /// @brief Add @p n to the transmitted-packets counter.
    void fetchTransmittedPackets(std::uint64_t n = 1) { __atomic_fetch_add(&transmittedPackets_, n, __ATOMIC_RELAXED); }
    /// @brief Add @p n to the transmitted-bytes counter.
    void fetchTransmittedBytes(std::uint64_t n = 1) { __atomic_fetch_add(&transmittedBytes_, n, __ATOMIC_RELAXED); }
    /// @brief Add @p n to the tx-dropped-packets counter.
    void fetchTxDroppedPackets(std::uint64_t n = 1) { __atomic_fetch_add(&txDroppedPackets_, n, __ATOMIC_RELAXED); }
    /// @brief Add @p n to the tx-dropped-bytes counter.
    void fetchTxDroppedBytes(std::uint64_t n = 1) { __atomic_fetch_add(&txDroppedBytes_, n, __ATOMIC_RELAXED); }
    /// @}

    /// @brief Reset every counter to zero (atomic store).
    void reset() {
        __atomic_store_n(&processedPackets_, 0, __ATOMIC_RELAXED);
        __atomic_store_n(&processedBytes_, 0, __ATOMIC_RELAXED);
        __atomic_store_n(&droppedPackets_, 0, __ATOMIC_RELAXED);
        __atomic_store_n(&droppedBytes_, 0, __ATOMIC_RELAXED);
        __atomic_store_n(&errors_, 0, __ATOMIC_RELAXED);
        __atomic_store_n(&overflows_, 0, __ATOMIC_RELAXED);
        __atomic_store_n(&transmittedPackets_, 0, __ATOMIC_RELAXED);
        __atomic_store_n(&transmittedBytes_, 0, __ATOMIC_RELAXED);
        __atomic_store_n(&txDroppedPackets_, 0, __ATOMIC_RELAXED);
        __atomic_store_n(&txDroppedBytes_, 0, __ATOMIC_RELAXED);
    }

private:
    std::uint64_t processedPackets_{0};
    std::uint64_t processedBytes_{0};
    std::uint64_t droppedPackets_{0};
    std::uint64_t droppedBytes_{0};
    std::uint64_t errors_{0};
    std::uint64_t overflows_{0};
    std::uint64_t transmittedPackets_{0};
    std::uint64_t transmittedBytes_{0};
    std::uint64_t txDroppedPackets_{0};
    std::uint64_t txDroppedBytes_{0};
};

/// @}

} // namespace vnic::stats
