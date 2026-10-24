#pragma once

#include <expected>
#include "error_codes.h"

#include <atomic>
#include <span>

#include <cstdint>

/**
 * @file
 * @brief CRTP state-machine base class (Stopped -> Configured -> Started).
 */

namespace vnic::utils {

/**
 * @defgroup group_utils Utilities
 * @brief State machine, FIFO, RCU wrapper and RAII helpers.
 * @{
 */

/**
 * @brief CRTP base implementing a configure/start/stop state machine.
 *
 * The derived type @c Derived must provide `configureImpl()`, `startImpl()` and
 * `stopImpl()`; the base calls them from the corresponding public methods while
 * performing state transitions atomically.
 *
 * Allowed transitions:
 * @code
 * Stopped --configure--> Configured --start--> Started --stop--> Stopped
 * @endcode
 * `configure()` is additionally allowed from @ref State::Configured and
 * @ref State::Error. A failing `*Impl()` moves the object to @ref State::Error.
 *
 * @tparam Derived The derived type (CRTP).
 */
template <typename Derived> class Stateful {
public:
    /// Lifecycle states.
    enum class State : std::uint8_t {
        Configuring = 0, ///< Configuration in progress.
        Configured,      ///< Configured, not started.
        Starting,        ///< Start in progress.
        Started,         ///< Running.
        Stopping,        ///< Stop in progress.
        Stopped,         ///< Stopped.
        Error,           ///< An operation failed.
    };

    Stateful() = default;

    /// @brief Copy-construct by copying the state of @p other.
    Stateful(const Stateful& other) { (void)this->operator=(other); }

    /// @brief Copy the state of @p other (other members are left untouched).
    Stateful& operator=(const Stateful& other) {
        state_ = static_cast<decltype(state_)>(other.getState());
        return *this;
    }

    /**
     * @brief Read the current state.
     * @param memoryOrder Memory order for the atomic load.
     */
    [[nodiscard]] State getState(std::memory_order memoryOrder = std::memory_order_acquire) const noexcept {
        const auto raw = __atomic_load_n(&state_, std_memory_order_to_int(memoryOrder));
        return static_cast<State>(raw);
    }

    /**
     * @brief Configure the object (calls `Derived::configureImpl()`).
     * @return `ErrorCode::StateConfigureNotAllowed` if the current state does
     *         not permit configuration, or the error from `configureImpl()`.
     */
    template <typename... Args> [[nodiscard]] std::expected<void, ErrorCode> configure(Args&&... args) {
        // Allowed from Stopped, Configured or Error.
        if (!cas({{State::Stopped, State::Configured, State::Error}}, State::Configuring)) [[unlikely]] {
            return std::unexpected(ErrorCode::StateConfigureNotAllowed);
        }

        auto result = self().configureImpl(std::forward<Args>(args)...);
        if (!result) [[unlikely]] {
            setState(State::Error);
            return result;
        }

        setState(State::Configured);
        return {};
    }

    /**
     * @brief Start the object (calls `Derived::startImpl()`).
     * @return `ErrorCode::StateStartNotAllowed` if the object is not configured,
     *         or the error from `startImpl()`.
     */
    template <typename... Args> [[nodiscard]] std::expected<void, ErrorCode> start(Args&&... args) {
        if (!cas(State::Configured, State::Starting)) [[unlikely]] {
            return std::unexpected(ErrorCode::StateStartNotAllowed);
        }

        // NOTE: the Started state is published BEFORE startImpl() is called.
        // For VNic, startImpl() launches the background RX poll loop that runs
        // while the state is Started and is stopped by stop() transitioning
        // Started -> Stopping. Publishing Started after startImpl() returned
        // would mean the loop never runs and stop() could not finish it.
        setState(State::Started);
        auto result = self().startImpl(std::forward<Args>(args)...);
        if (!result) [[unlikely]] {
            setState(State::Error);
            return result;
        }

        return {};
    }

    /**
     * @brief Stop the object (calls `Derived::stopImpl()`).
     * @return `ErrorCode::StateStopNotAllowed` if the object is not started, or
     *         the error from `stopImpl()`.
     */
    template <typename... Args> [[nodiscard]] std::expected<void, ErrorCode> stop(Args&&... args) {
        if (!cas(State::Started, State::Stopping)) [[unlikely]] {
            return std::unexpected(ErrorCode::StateStopNotAllowed);
        }

        auto result = self().stopImpl(std::forward<Args>(args)...);
        if (!result) [[unlikely]] {
            setState(State::Error);
            return result;
        }

        setState(State::Stopped);
        return {};
    }

protected:
    /// @brief Store a new state atomically.
    void setState(State newState, std::memory_order memoryOrder = std::memory_order_release) noexcept {
        __atomic_store_n(&state_, static_cast<std::uint8_t>(newState), std_memory_order_to_int(memoryOrder));
    }

    /// @brief Compare-and-swap the state from @p expected to @p newState.
    [[nodiscard]] bool cas(State expected, State newState) noexcept {
        auto expectedRaw      = static_cast<std::uint8_t>(expected);
        const auto desiredRaw = static_cast<std::uint8_t>(newState);
        return __atomic_compare_exchange_n(&state_, &expectedRaw, desiredRaw,
                                           false, // weak = false (strong)
                                           __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE);
    }

    /// @brief Try to swap the state from any of @p expected to @p newState.
    [[nodiscard]] bool cas(std::span<const State> expected, State newState) noexcept {
        // Try each expectation.
        for (auto exp : expected) {
            if (cas(exp, newState)) {
                return true;
            }
        }
        return false;
    }

private:
    static constexpr int std_memory_order_to_int(std::memory_order order) noexcept {
        switch (order) {
        case std::memory_order_relaxed: return __ATOMIC_RELAXED;
        case std::memory_order_consume: return __ATOMIC_CONSUME;
        case std::memory_order_acquire: return __ATOMIC_ACQUIRE;
        case std::memory_order_release: return __ATOMIC_RELEASE;
        case std::memory_order_acq_rel: return __ATOMIC_ACQ_REL;
        case std::memory_order_seq_cst: return __ATOMIC_SEQ_CST;
        }
        return __ATOMIC_SEQ_CST;
    }

    Derived& self() noexcept { return *static_cast<Derived*>(this); }

    const Derived& self() const noexcept { return *static_cast<const Derived*>(this); }

    mutable std::uint8_t state_{static_cast<std::uint8_t>(State::Stopped)};
};

/// @}

} // namespace vnic::utils
